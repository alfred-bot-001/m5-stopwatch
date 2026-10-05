"""Flash only receiver C480 after verifying its complete original backup."""
import hashlib,json,subprocess,sys,time
from pathlib import Path
import serial
from serial.tools.list_ports import comports
from archive_build import archive_build
root=Path(__file__).resolve().parents[1]
backup=root.parent/'backups/receiver-9c139e8ac480/original-20261005.bin'
meta=json.loads(backup.with_suffix('.json').read_text())
assert meta['mac']=='9c:13:9e:8a:c4:80'
assert backup.stat().st_size==16777216 and hashlib.sha256(backup.read_bytes()).hexdigest()==meta['sha256']
archive_build(root, root / 'receiver/build', 'stopwatch_receiver')
app=next((p for p in comports() if p.vid==0xcafe and p.pid==0x4021 and p.serial_number=='9C139E8AC480-VIBE1'),None)
if app:
 with serial.Serial(app.device,115200,timeout=2) as s:
  status=b''.join(s.readline() for _ in range(3));assert b'VIBE RX v=1 ' in status
  s.write(b'\nVIBE/1 ARM-BOOT 9C139E8AC480\nVIBE/1 CONFIRM-BOOT 9C139E8AC480\n');s.flush()
 deadline=time.monotonic()+10
 while time.monotonic()<deadline:
  if any(p.vid==0x303a and (p.serial_number or '').lower()==meta['mac'] for p in comports()):break
  time.sleep(.25)
rom=next(p for p in comports() if p.vid==0x303a and (p.serial_number or '').lower()==meta['mac'])
base=[sys.executable,'-m','esptool','--chip','esp32s3','--port',rom.device]
identity=subprocess.check_output(base+['--after','no-reset','read-mac'],text=True)
assert meta['mac'] in identity.lower();print(identity)
build=root/'receiver/build'
subprocess.run(base+['--after','watchdog-reset','write-flash','--flash-mode','dio','--flash-size','16MB','--flash-freq','80m','0x0',str(build/'bootloader/bootloader.bin'),'0x8000',str(build/'partition_table/partition-table.bin'),'0x10000',str(build/'stopwatch_receiver.bin')],check=True)
