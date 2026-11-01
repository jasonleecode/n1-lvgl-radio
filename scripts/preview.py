#!/usr/bin/env python3
"""Render on an isolated X server, capture the actual framebuffer, no UI automation."""
from pathlib import Path
import os,struct,subprocess,tempfile,time,zlib,sys
out=Path('/home/n1/.cache/n1-radio');out.mkdir(parents=True,exist_ok=True)
def chunk(tag,b):return struct.pack('>I',len(b))+tag+b+struct.pack('>I',zlib.crc32(tag+b)&0xffffffff)
with tempfile.TemporaryDirectory(prefix='n1-radio-preview-') as folder:
 with (out/'xvfb.log').open('w') as log:
  server=subprocess.Popen(['Xvfb','-displayfd','1','-screen','0','520x400x24','-nolisten','tcp','-fbdir',folder],stdout=subprocess.PIPE,stderr=log,text=True)
  app=None
  try:
   display=server.stdout.readline().strip();assert display.isdigit()
   env=dict(os.environ,DISPLAY=':'+display,SDL_VIDEODRIVER='x11',SDL_RENDER_DRIVER='software')
   with (out/'preview.log').open('w') as app_log:
    app=subprocess.Popen(['/opt/n1-radio/bin/n1-radio','--demo','--exit-after','7']+sys.argv[1:],env=env,stdout=app_log,stderr=subprocess.STDOUT)
    time.sleep(3);assert app.poll() is None,app.returncode
    data=(Path(folder)/'Xvfb_screen0').read_bytes();h=struct.unpack('>25I',data[:100]);header,version,fmt,depth,w,height,xoffset,order,unit,bitorder,pad,bpp,stride,visual,red,green,blue,bits,entries,colors=h[:20]
    assert version==7 and bpp==32
    pixels=data[header+colors*12:];rows=[]
    for y in range(height):
     row=bytearray()
     for x in range(w):
      offset=y*stride+x*4;p=int.from_bytes(pixels[offset:offset+4],'little' if order==0 else 'big');row.extend(((p>>16)&255,(p>>8)&255,p&255))
     rows.append(b'\0'+row)
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',w,height,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(rows)))+chunk(b'IEND',b'')
    (out/'preview.png').write_bytes(png);print('Rendered',w,height,'to',out/'preview.png')
    assert app.wait(timeout=10)==0
  finally:
   if app and app.poll() is None:app.terminate();app.wait(timeout=5)
   server.terminate();server.wait(timeout=5)
