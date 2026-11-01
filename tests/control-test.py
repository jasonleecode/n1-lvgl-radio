#!/usr/bin/env python3
"""Exercise real headless app, CLI, private socket and persisted configuration."""
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

binary, cli = sys.argv[1:]
with tempfile.TemporaryDirectory(prefix='n1-ctl-') as temp:
    env = dict(os.environ, XDG_RUNTIME_DIR=temp, XDG_CONFIG_HOME=temp)
    env.pop('DISPLAY', None)
    process = subprocess.Popen([binary, '--headless'], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    path = str(Path(temp) / 'n1-radio/control.sock')

    def wire(data):
        with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as s:
            s.settimeout(3)
            s.connect(path)
            s.sendall(data)
            return json.loads(s.recv(16384))

    def command(*args, ok=True):
        r = subprocess.run([sys.executable, cli, '--json', *args], env=env, capture_output=True, text=True, timeout=5)
        assert (r.returncode == 0) == ok, (args, r.stdout, r.stderr)
        return json.loads(r.stdout) if r.stdout else None

    try:
        for _ in range(200):
            if Path(path).exists():
                break
            assert process.poll() is None
            time.sleep(.01)
        assert (os.stat(path).st_mode & 0o777) == 0o600
        assert command('status')['frequency_hz'] == 100000000
        assert not command('status')['active']
        # Single instance cannot steal the socket or overwrite the first config.
        second = subprocess.run([binary, '--headless'], env=env, capture_output=True, timeout=3)
        assert second.returncode != 0
        assert command('volume', '42')['ok']
        command('mute', 'on')
        assert command('status')['volume'] == 42
        assert command('status')['mute']
        command('stereo', 'mono')
        assert command('status')['stereo_mode'] == 'mono'
        assert not command('status')['stereo']
        command('stereo', 'auto')
        assert command('status')['stereo_mode'] == 'auto'
        command('mode', 'NFM')
        assert command('status')['mode'] == 'NFM'
        # Invalid values, oversized or malformed frames leave state unchanged.
        assert not wire(b'{"command":"volume","value":4294967297}')['ok']
        assert not wire(b'{"command":"tune","value":-1}')['ok']
        assert not wire(b'{"command":"tune","value":97400000,"mode":false}')['ok']
        assert not wire(b'{broken')['ok']
        assert not wire(b'x' * 6000)['ok']
        assert not wire(b'{"command":"stop"}junk')['ok']
        command('tune', 'nan', ok=False)
        command('volume', '101', ok=False)
        command('tune', '7.1M', '--mode', 'AM', ok=False)  # Tuner cannot receive HF without appropriate hardware.
        command('tune', '97.4M', '--mode', 'FM')
        assert command('status')['frequency_hz'] == 97400000
        command('step', '--', '-100k')
        state = command('status')
        assert state['frequency_hz'] == 97300000 and state['mode'] == 'FM'
        command('stop')
        assert not command('status')['active']
        assert command('presets')['presets'] == []
        command('preset', '1', ok=False)
        def ipc(**payload):
            return wire(json.dumps(payload,ensure_ascii=False,separators=(',',':')).encode())
        for bad in ['', '  ', 'bad\nname', '零'*27, 'a\x00b']:
            assert not ipc(command='preset_save',value=97400000,mode='FM',name=bad)['ok']
        assert not wire(b'{"command":"preset_save","value":97400000,"mode":"FM","name":"\xff"}')['ok']
        assert ipc(command='preset_save',value=97400000,mode='FM',name='测试甲')['ok']
        assert ipc(command='preset_save',value=99600000,mode='FM',name='测试乙')['ok']
        assert ipc(command='preset_save',value=97400000,mode='FM',name='甲改名')['ok']
        saved=command('presets')['presets'];assert len(saved)==2 and saved[0]['name']=='甲改名'
        # Saving presets must not retune or start the receiver.
        assert command('status')['frequency_hz']==97300000 and not command('status')['active']
        assert ipc(command='preset_delete',value=1,frequency_hz=97400000,mode='FM')['ok']
        # A different client still holding index 1 cannot delete its new occupant.
        assert not ipc(command='preset_delete',value=1,frequency_hz=97400000,mode='FM')['ok']
        assert command('presets')['presets'][0]['name']=='测试乙'
        assert ipc(command='preset_delete',value=1,frequency_hz=99600000,mode='FM')['ok']
        for i in range(16):assert ipc(command='preset_save',value=90000000+i*100000,mode='FM',name='收藏'+str(i))['ok']
        assert not ipc(command='preset_save',value=100000000,mode='FM',name='超额')['ok']
        assert len(command('presets')['presets'])==16
        command('quit')
        process.communicate(timeout=8)
        assert process.returncode == 0
        assert not Path(path).exists()
        settings = (Path(temp) / 'n1-radio/settings.ini').read_text()
        assert 'frequency=97300000' in settings and 'volume=42' in settings
        assert settings.count('station=')==16 and '收藏15' in settings
        # A stale socket left after SIGKILL must be recoverable.
        old = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        old.bind(path)
        old.close()
        restarted = subprocess.Popen([binary, '--headless', '--exit-after', '1'], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        restarted.communicate(timeout=5)
        assert restarted.returncode == 0
    finally:
        if process.poll() is None:
            process.terminate()
            process.communicate(timeout=8)
print('PASS: headless CLI, shared state, validation, single instance, persistence and stale-socket recovery')
