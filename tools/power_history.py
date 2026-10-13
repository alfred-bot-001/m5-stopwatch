"""Export this StopWatch's persistent power history without resetting it."""
import argparse
import csv
from datetime import datetime, timezone
import io
import json
from pathlib import Path
import time

import serial
from serial.tools.list_ports import comports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, help='New CSV file (never overwritten)')
    args = parser.parse_args()
    output = Path(args.output)
    if output.exists() or output.with_suffix('.json').exists():
        parser.error('Output already exists; choose a new filename')
    ports = [p for p in comports() if p.vid == 0xcafe and p.pid == 0x4020
             and p.serial_number == '288485439560-VIBE1']
    if len(ports) != 1:
        parser.error('Exactly one verified StopWatch application USB port is required')
    started = datetime.now(timezone.utc).isoformat()
    lines, status = [], None
    with serial.Serial(port=None, baudrate=115200, timeout=1) as conn:
        conn.dtr = True
        conn.rts = False
        conn.port = ports[0].device
        conn.open()
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            line = conn.readline().decode('ascii', errors='strict').strip()
            if line.startswith('VIBE '):
                status = dict(word.split('=', 1) for word in line.split() if '=' in word)
                break
        if status is None:
            raise RuntimeError('No application telemetry; no commands sent')
        conn.write(b'\nH')
        conn.flush()
        begun = False
        deadline = time.monotonic() + 120
        while time.monotonic() < deadline:
            line = conn.readline().decode('ascii', errors='strict')
            if line.strip() == 'POWER_HISTORY BUSY':
                raise RuntimeError('History is recovering, busy or unavailable; retry later')
            if line.strip() == 'POWER_HISTORY 1':
                begun = True
                continue
            if not begun:
                continue
            if line.strip() == 'POWER_HISTORY END':
                break
            if line:
                lines.append(line)
        else:
            raise TimeoutError('Incomplete history export; no output saved')
    body = ''.join(lines)
    errors = [line for line in lines if line.startswith('# error=')]
    if errors:
        raise RuntimeError('Device reported an incomplete export: ' + ''.join(errors))
    rows = list(csv.DictReader(io.StringIO(''.join(line for line in lines if not line.startswith('#')))))
    if not lines or not any(',' in line for line in lines):
        raise RuntimeError('Missing CSV header; no output saved')
    format_lines = [line for line in lines if line.startswith('# format=power_history_1 ')]
    if len(format_lines) != 1:
        raise RuntimeError('Missing or duplicate snapshot metadata')
    snapshot = dict(word.split('=', 1) for word in format_lines[0].split() if '=' in word)
    if len(rows) != int(snapshot['records']):
        raise RuntimeError('Incomplete snapshot record count')
    sequences = [int(row['seq']) for row in rows]
    if any(None in row or None in row.values() for row in rows) or any(a >= b for a, b in zip(sequences, sequences[1:])):
        raise RuntimeError('Malformed CSV or out-of-order snapshot')
    with output.open('x') as dest:
        dest.write(body)
    metadata = {
        'device': ports[0].serial_number,
        'export_started_utc': started,
        'export_completed_utc': datetime.now(timezone.utc).isoformat(),
        'rows': len(rows),
        'snapshot': snapshot,
        'status_before_export': status,
        'time_note': 'Records use boot session + device uptime. Export UTC is host time, not each historical sample time.',
        'measurement_note': 'Percent is estimated from voltage. Battery current and consumed mAh are not measured.',
    }
    with output.with_suffix('.json').open('x') as dest:
        json.dump(metadata, dest, ensure_ascii=False, indent=2)
        dest.write('\n')
    print(f'{len(rows)} records: {output.resolve()}')


if __name__ == '__main__':
    main()
