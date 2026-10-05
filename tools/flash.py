"""Flash only the backed-up StopWatch, never another attached ESP32."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time
import serial
from serial.tools.list_ports import comports
from archive_build import archive_build

root = Path(__file__).resolve().parents[1]
backup = root / 'backups/original-288485439560-20261003.bin'
manifest = json.loads(backup.with_suffix('.json').read_text())
assert backup.stat().st_size == 16777216
assert hashlib.sha256(backup.read_bytes()).hexdigest() == manifest['sha256']
archive_build(root, root / 'build', 'stopwatch_vibe')
for port in comports():
    if port.vid == 0xcafe and port.pid == 0x4020 and port.serial_number == '288485439560-VIBE1':
        with serial.Serial(port.device, 115200, timeout=1) as conn:
            status = b''.join(conn.readline() for _ in range(3))
            if b'VIBE v=4 ' in status or b'VIBE v=3 ' in status:
                conn.write(b'B')  # One-time migration from the old firmware.
            elif any(f'VIBE v={v} '.encode() in status for v in (5, 6, 7, 8, 9, 10, 11)):
                conn.write(b'\nVIBE/1 ARM-BOOT 288485439560\nVIBE/1 CONFIRM-BOOT 288485439560\n')
            else:
                sys.exit('Unknown running firmware; refusing to send a boot command')
            conn.flush()
        break
deadline = time.monotonic() + 15
port = None
while time.monotonic() < deadline:
    port = next((p.device for p in comports() if p.serial_number == manifest['mac'] and p.vid == 0x303a), None)
    if port:
        break
    time.sleep(.25)
if not port:
    sys.exit('Verified StopWatch ROM port not found. Hold BOOT while resetting the device.')
base = [sys.executable, '-m', 'esptool', '--chip', 'esp32s3', '--port', port, '--baud', '115200']
identity = subprocess.check_output(base + ['--after', 'no-reset', 'read-mac'], text=True)
print(identity)
assert manifest['mac'] in identity, 'Wrong chip; flash aborted'
subprocess.run(base + ['--after', 'watchdog-reset', 'write-flash', '--flash-mode', 'dio',
    '--flash-size', '16MB', '--flash-freq', '80m', '0x0', str(root/'build/bootloader/bootloader.bin'),
    '0x8000', str(root/'build/partition_table/partition-table.bin'),
    '0x10000', str(root/'build/stopwatch_vibe.bin')], check=True)
