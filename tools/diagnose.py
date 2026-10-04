"""Bounded, read-only diagnostics for this StopWatch; never opens its ROM port."""
import argparse
from datetime import datetime, timezone
import json
import math
import os
import signal
import time
import serial
from serial.tools.list_ports import comports

def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds', type=int, default=86400)
    parser.add_argument('--deadline-unix', type=float,
                        help='Shared absolute Unix deadline across supervisor restarts')
    parser.add_argument('--output', required=True)
    args = parser.parse_args(argv)
    if not 1 <= args.seconds <= 86400:
        parser.error('seconds must be between 1 and 86400')
    if args.deadline_unix is not None and not math.isfinite(args.deadline_unix):
        parser.error('deadline-unix must be a finite Unix timestamp')
    return args


def main(argv=None):
    args = parse_args(argv)
    started_wall, started_mono = time.time(), time.monotonic()
    deadline_wall = min(started_wall + args.seconds,
                        args.deadline_unix if args.deadline_unix is not None else math.inf)
    duration = max(0.0, deadline_wall - started_wall)
    deadline_mono = started_mono + duration
    stop_signal = None

    def handle_signal(number, _frame):
        nonlocal stop_signal
        stop_signal = number

    with open(args.output, 'a', buffering=1) as log:
        def emit(kind, **fields):
            log.write(json.dumps({'utc': datetime.now(timezone.utc).isoformat(),
                                  'kind': kind, **fields}) + '\n')

        connection = None

        def close_connection():
            nonlocal connection
            if connection is not None:
                try:
                    connection.close()
                except (OSError, serial.SerialException) as error:
                    emit('serial_error', operation='close', error=str(error))
                finally:
                    connection = None

        previous = None
        last_sample = 0
        previous_flags = None
        last_status_wall = None
        heartbeat_wall, heartbeat_mono = started_wall, started_mono
        previous_handlers = {}
        exit_reason, exit_code = 'deadline', 0
        emit('start', pid=os.getpid(), requested_seconds=args.seconds,
             duration_seconds=duration, deadline_unix=deadline_wall)
        try:
            for number in (signal.SIGINT, signal.SIGTERM):
                previous_handlers[number] = signal.signal(number, handle_signal)
            while True:
                now_wall, now_mono = time.time(), time.monotonic()
                # Either clock can reveal a host sleep/resume gap. The wall deadline
                # also prevents a supervised restart from beginning a fresh 24 hours.
                if now_mono - heartbeat_mono >= 30 or now_wall - heartbeat_wall >= 30:
                    emit('heartbeat', pid=os.getpid(), mode=previous[0] if previous else 'unknown',
                         port=previous[1] if previous else None,
                         elapsed_seconds=now_mono - started_mono,
                         wall_gap_seconds=now_wall - heartbeat_wall,
                         monotonic_gap_seconds=now_mono - heartbeat_mono,
                         last_status_unix=last_status_wall,
                         deadline_unix=deadline_wall)
                    heartbeat_wall, heartbeat_mono = now_wall, now_mono
                if stop_signal is not None:
                    exit_reason = signal.Signals(stop_signal).name
                    exit_code = 128 + stop_signal
                    emit('signal', signal=exit_reason)
                    break
                if now_wall >= deadline_wall or now_mono >= deadline_mono:
                    break
                ports = list(comports())
                app = next((p for p in ports if p.vid == 0xcafe and p.pid == 0x4020
                            and p.serial_number == '288485439560-VIBE1'), None)
                rom = next((p for p in ports if p.vid == 0x303a and p.pid == 0x1001
                            and p.serial_number == '28:84:85:43:95:60'), None)
                state = ('app', app.device) if app else ('rom', rom.device) if rom else ('absent', None)
                if state != previous:
                    emit('usb', mode=state[0], port=state[1])
                    close_connection()
                    previous_flags = None
                    previous = state
                if app:
                    try:
                        if connection is None:
                            connection = serial.Serial(port=None, baudrate=115200, timeout=1)
                            # App CDC requires DTR for telemetry. Never open the ROM.
                            connection.dtr = True
                            connection.rts = False
                            connection.port = app.device
                            connection.open()
                        line = connection.readline().decode('ascii', errors='replace').strip()
                        if line.startswith('VIBE '):
                            last_status_wall = time.time()
                            fields = dict(word.split('=', 1) for word in line.split() if '=' in word)
                            flags = tuple(fields.get(k) for k in (
                                'g0', 'pm_btn', 'pm_cfg', 'pm_sleep', 'pm_wdt', 'pm_timer',
                                'pm_src', 'pm_wake', 'errors', 'usb', 'pm_events', 'pm_evt_ms',
                                'pm_hold_max', 'intent', 'boot_raw', 'boot_strap', 'prev'))
                            if flags != previous_flags or time.monotonic() - last_sample >= 5:
                                emit('status', value=line)
                                last_sample = time.monotonic()
                                previous_flags = flags
                    except (OSError, serial.SerialException) as error:
                        emit('serial_error', error=str(error))
                        close_connection()
                        time.sleep(1)
                else:
                    time.sleep(1)
        except Exception as error:
            exit_reason, exit_code = 'exception', 1
            emit('outer_error', error_type=type(error).__name__, error=str(error))
        finally:
            close_connection()
            emit('stop', reason=exit_reason, exit_code=exit_code,
                 elapsed_seconds=time.monotonic() - started_mono,
                 wall_elapsed_seconds=time.time() - started_wall)
            for number, handler in previous_handlers.items():
                signal.signal(number, handler)
    return exit_code


if __name__ == '__main__':
    raise SystemExit(main())
