#!/usr/bin/python3
from pathlib import Path
import shutil,subprocess
base=Path(__file__).resolve().parent
Path('/opt/n1-radio/bin').mkdir(parents=True,exist_ok=True)
shutil.copy2(base/'n1-radio','/opt/n1-radio/bin/n1-radio.next');Path('/opt/n1-radio/bin/n1-radio.next').chmod(0o755);Path('/opt/n1-radio/bin/n1-radio.next').replace('/opt/n1-radio/bin/n1-radio')
if (base/'redsea').exists():
 shutil.copy2(base/'redsea','/opt/n1-radio/bin/redsea.next');Path('/opt/n1-radio/bin/redsea.next').chmod(0o755);Path('/opt/n1-radio/bin/redsea.next').replace('/opt/n1-radio/bin/redsea')
shutil.copy2(base/'launcher','/usr/local/bin/n1-radio');Path('/usr/local/bin/n1-radio').chmod(0o755)
for src,dest in [('n1-radio.desktop','/usr/local/share/applications/n1-radio.desktop'),('n1-radio.svg','/usr/local/share/icons/hicolor/scalable/apps/n1-radio.svg')]:
 p=Path(dest);p.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(base/src,p)
subprocess.run(['update-desktop-database','/usr/local/share/applications'],check=True)
licenses=Path('/opt/n1-radio/share/licenses');licenses.mkdir(parents=True,exist_ok=True)
for name in ['LICENSE','FONT-LICENSE.txt','LVGL-LICENSE.txt','REDSEA-LICENSE.txt']:
 if (base/name).exists():shutil.copy2(base/name,licenses/name)
print('Installed N1 Radio and application menu entry')
