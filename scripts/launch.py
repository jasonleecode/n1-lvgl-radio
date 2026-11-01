#!/usr/bin/python3
from pathlib import Path
import subprocess,sys
pid=subprocess.check_output(['pgrep','-u','n1','-x','xfce4-session'],text=True).splitlines()[0]
env=dict(x.decode().split('=',1) for x in Path('/proc/'+pid+'/environ').read_bytes().split(b'\0') if b'=' in x)
keys=['DISPLAY','XAUTHORITY','DBUS_SESSION_BUS_ADDRESS','XDG_RUNTIME_DIR','HOME','LANG']
args=['runuser','-u','n1','--','env']+[k+'='+env[k] for k in keys if k in env]
with Path('/home/n1/.cache/n1-radio-console.log').open('w') as log:
 p=subprocess.Popen(args+['/usr/local/bin/n1-radio']+sys.argv[1:],stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
print('Launcher PID:',p.pid)
