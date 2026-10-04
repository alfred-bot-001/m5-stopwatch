"""Bounded, read-only diagnostics for this StopWatch; never opens its ROM port."""
import argparse
from datetime import datetime, timezone
import json
import time
import serial
from serial.tools.list_ports import comports

parser = argparse.ArgumentParser()
parser.add_argument('--seconds', type=int, default=86400)
parser.add_argument('--output', required=True)
args = parser.parse_args()
if not 1 <= args.seconds <= 86400:
    parser.error('seconds must be between 1 and 86400')

with open(args.output, 'a', buffering=1) as log:
    def emit(kind, **fields):
        log.write(json.dumps({'utc': datetime.now(timezone.utc).isoformat(),
                              'kind': kind, **fields}) + '\n')

    deadline = time.monotonic() + args.seconds
    connection = None
    previous = None
    last_sample = 0
    previous_flags = None
    emit('start', duration_seconds=args.seconds)
    try:
        while time.monotonic() < deadline:
            ports = list(comports())
            app = next((p for p in ports if p.vid == 0xcafe and p.pid == 0x4020
                        and p.serial_number == '288485439560-VIBE1'), None)
            rom = next((p for p in ports if p.vid == 0x303a and p.pid == 0x1001
                        and p.serial_number == '28:84:85:43:95:60'), None)
            state = ('app', app.device) if app else ('rom', rom.device) if rom else ('absent', None)
            if state != previous:
                emit('usb', mode=state[0], port=state[1])
                if connection:
                    connection.close()
                    connection = None
                previous = state
            if app:
                try:
                    if connection is None:
                        connection = serial.Serial(port=None, baudrate=115200, timeout=1)
                        # App CDC requires DTR for telemetry. The ROM port is never opened.
                        connection.dtr = True
                        connection.rts = False
                        connection.port = app.device
                        connection.open()
                    line = connection.readline().decode('ascii', errors='replace').strip()
                    if line.startswith('VIBE '):
                        fields = dict(word.split('=', 1) for word in line.split() if '=' in word)
                        flags = tuple(fields.get(k) for k in ('g0', 'pm_btn', 'pm_cfg', 'pm_sleep',
                                      'pm_wdt', 'pm_timer', 'pm_src', 'pm_wake', 'errors', 'usb'))
                        if flags != previous_flags or time.monotonic() - last_sample >= 5:
                            emit('status', value=line)
                            last_sample = time.monotonic()
                            previous_flags = flags
                except (OSError, serial.SerialException) as error:
                    emit('serial_error', error=str(error))
                    if connection:
                        connection.close()
                        connection = None
                    time.sleep(1)
            else:
                time.sleep(1)
    finally:
        if connection:
            connection.close()
        emit('stop')
