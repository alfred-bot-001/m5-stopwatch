"""Read a diagnostic framebuffer from one verified device and encode a PNG."""
import argparse,struct,zlib
from pathlib import Path
import serial
from serial.tools.list_ports import comports
p=argparse.ArgumentParser();device=p.add_mutually_exclusive_group();device.add_argument('--receiver',action='store_true');device.add_argument('--geek',action='store_true');p.add_argument('--output',required=True);args=p.parse_args()
identity,pid=('D4059278988C-VIBE1',0x4022) if args.geek else ('9C139E8AC480-VIBE1',0x4021) if args.receiver else ('288485439560-VIBE1',0x4020)
port=next(p for p in comports() if p.vid==0xcafe and p.pid==pid and p.serial_number==identity)
with serial.Serial(port.device,115200,timeout=3) as s:
 s.write(b'P');s.flush()
 for _ in range(10):
  line=s.readline()
  if line.startswith(b'FRAME '):break
 else:raise RuntimeError('No frame header')
 _,w,h,n=line.split();w,h,n=map(int,(w,h,n));assert n==w*h*2 and 0<w<=466 and 0<h<=466
 raw=s.read(n);assert len(raw)==n
pixels=bytearray()
for y in range(h):
 pixels.append(0)
 for x in range(w):
  i=(y*w+x)*2;v=int.from_bytes(raw[i:i+2],'big');pixels.extend((((v>>11)&31)*255//31,((v>>5)&63)*255//63,(v&31)*255//31))
def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
Path(args.output).write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(pixels))+chunk(b'IEND',b''))
print(f'{w}x{h}: {args.output}')
