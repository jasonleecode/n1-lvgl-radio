#!/usr/bin/env python3
"""Black-box LAN web/audio check. Requires a receiving radio and ffprobe/ffmpeg."""
import argparse
import array
import concurrent.futures
import json
from pathlib import Path
import subprocess
import tempfile
import time
import urllib.error
import urllib.request

p=argparse.ArgumentParser();p.add_argument('--url',default='http://127.0.0.1:8090');a=p.parse_args()
def get(path):return urllib.request.urlopen(a.url+path,timeout=12)
for path,mime in [('/', 'text/html'),('/style.css','text/css'),('/app.js','text/javascript')]:
 with get(path) as response:
  assert response.headers['Content-Type'].startswith(mime)
  assert len(response.read())>100
  assert response.headers['Cache-Control']=='no-store'
try:
 get('/not-found')
 raise AssertionError('Missing routes must return 404')
except urllib.error.HTTPError as error:
 assert error.code==404
for _ in range(60):
 with get('/api/status') as response:status=json.load(response)
 if status.get('receiving'):break
 time.sleep(.1)
assert status.get('receiving'),status
# Two simultaneous listeners must each receive complete, decodable stereo MP3.
def listen(_):
 started=time.monotonic()
 with get('/stream.mp3') as response:
  assert response.headers['Content-Type']=='audio/mpeg'
  data=response.read(64000)
  elapsed=time.monotonic()-started
  assert elapsed<8,("Audio delivery is slower than realtime",elapsed)
  return data
with concurrent.futures.ThreadPoolExecutor(2) as pool:
 samples=list(pool.map(listen,range(2)))
with tempfile.TemporaryDirectory(prefix='radio-web-check-') as temp:
 for number,data in enumerate(samples):
  assert len(data)==64000
  file=Path(temp)/f'stream{number}.mp3';file.write_bytes(data)
  info=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_entries','stream=codec_name,sample_rate,channels','-of','json',str(file)]))['streams'][0]
  assert info=={'codec_name':'mp3','sample_rate':'48000','channels':2},info
  pcm=subprocess.check_output(['ffmpeg','-v','error','-i',str(file),'-f','s16le','-ar','48000','-ac','2','pipe:1'])
  audio=array.array('h',pcm);assert len(audio)>48000
  assert max(abs(v) for v in audio)>10,'MP3 is silent'
print('PASS: routes, live status, two simultaneous listeners, decodable 48kHz stereo MP3 with nonzero audio')
