#!/usr/bin/env python3
"""HTTP -> Unix IPC -> real headless host -> persisted presets, without touching user data."""
import concurrent.futures
import importlib.machinery
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time
import types
import urllib.error
import urllib.request

binary, script, assets = sys.argv[1:]
loader=importlib.machinery.SourceFileLoader('radio_web',script)
module=types.ModuleType(loader.name);loader.exec_module(module)
with tempfile.TemporaryDirectory(prefix='radio-http-') as temp:
 os.environ['XDG_RUNTIME_DIR']=temp;os.environ['XDG_CONFIG_HOME']=temp
 host=subprocess.Popen([binary,'--headless'],env=os.environ,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 server=module.PlayerServer(('127.0.0.1',0),module.Handler);server.assets=Path(assets);server.broadcast=module.Broadcast()
 thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
 origin='http://127.0.0.1:'+str(server.server_port)
 def get(path):
  with urllib.request.urlopen(origin+path,timeout=4) as response:return json.load(response)
 def post(payload,request_origin=origin,content_type='application/json',expected=200):
  headers={'Content-Type':content_type}
  if request_origin is not None:headers['Origin']=request_origin
  request=urllib.request.Request(origin+'/api/control',json.dumps(payload).encode(),headers,method='POST')
  try:
   with urllib.request.urlopen(request,timeout=5) as response:
    assert response.status==expected;return json.load(response)
  except urllib.error.HTTPError as error:
   assert error.code==expected,(error.code,expected);return json.load(error)
 try:
  for _ in range(200):
   if (Path(temp)/'n1-radio/control.sock').exists():break
   time.sleep(.01)
  assert not post({'command':'tune','value':97400000},None,expected=403)['ok']
  assert not post({'command':'tune','value':97400000},'http://foreign.test',expected=403)['ok']
  assert not post({'command':'tune','value':97400000},content_type='text/plain',expected=415)['ok']
  assert not post({'command':'quit'},expected=400)['ok']
  assert not post({'command':'tune','value':-1})['ok']
  assert post({'command':'tune','value':97400000,'mode':'FM'})['ok']
  assert get('/api/status')['frequency_hz']==97400000
  assert post({'command':'step','value':100000})['ok']
  assert get('/api/status')['frequency_hz']==97500000
  assert post({'command':'preset_save','value':97500000,'mode':'FM','name':'网页收藏'})['ok']
  assert post({'command':'preset_save','value':97500000,'mode':'FM','name':'改名收藏'})['ok']
  with concurrent.futures.ThreadPoolExecutor(2) as pool:
   a,b=list(pool.map(lambda _:get('/api/presets'),range(2)))
  assert a==b and len(a['presets'])==1 and a['presets'][0]['name']=='改名收藏'
  # A different local controller changes the same host; HTTP reads it back.
  assert module.radio_command({'command':'preset_save','value':99600000,'mode':'FM','name':'主机收藏'})['ok']
  assert get('/api/presets')['presets'][1]['name']=='主机收藏'
  # Persistence and no duplicated records, not a browser-local favorites cache.
  path=Path(temp)/'n1-radio/settings.ini'
  for _ in range(100):
   if path.exists() and '主机收藏' in path.read_text():break
   time.sleep(.01)
  assert '网页收藏' not in path.read_text() and '改名收藏' in path.read_text()
  assert post({'command':'preset_delete','value':1,'frequency_hz':97500000,'mode':'FM'})['ok']
  assert not post({'command':'preset_delete','value':1,'frequency_hz':97500000,'mode':'FM'})['ok']
  assert get('/api/presets')['presets'][0]['name']=='主机收藏'
 finally:
  server.shutdown();server.server_close();thread.join(timeout=3)
  host.terminate();host.communicate(timeout=8)
 assert host.returncode==0
print('PASS: HTTP tune/step, shared host presets, rename/dedup, local-to-web sync, persistence and request validation')
