#!/usr/bin/env python3
from fontTools.ttLib import TTFont
from fontTools import subset
from pathlib import Path
import subprocess,sys
root=Path(__file__).resolve().parent.parent
source=root/'.build-support/NotoSansCJK-Regular.ttc'
font=TTFont(source,fontNumber=2)
characters=set(chr(i) for i in range(32,127))|set('…·−×℃–')
for a in range(0xa1,0xf8):
 for b in range(0xa1,0xff):
  try:characters.add(bytes([a,b]).decode('gb2312'))
  except UnicodeDecodeError:pass
options=subset.Options();options.desubroutinize=True
sub=subset.Subsetter(options=options);sub.populate(text=''.join(sorted(characters)));sub.subset(font)
out=root/'.build-support/noto-subset.otf';font.save(out)
conv=root/'.build-support/font-tools/node_modules/.bin/lv_font_conv'
ui=set(''.join(p.read_text() for p in (root/'src').glob('*.c') if 'font' not in p.name)) & characters
first=set(sorted(characters-ui)[:max(0,3500-len(ui))])|ui
second=characters-first
specs=[(20,'radio_font_extra_20',''.join(sorted(second)),'lv_font_montserrat_14'),
       (20,'radio_font_20',''.join(sorted(first)),'radio_font_extra_20'),
       (76,'radio_font_76','0123456789.-','lv_font_montserrat_14')]
for size,name,chars,fallback in specs:
 target=root/'src'/f'{name}.c'
 subprocess.run([str(conv),'--font',str(out),'--size',str(size),'--bpp','4','--format','lvgl','--no-compress','--symbols',chars,'--lv-font-name',name,'--lv-fallback',fallback,'--lv-include','lvgl/lvgl.h','-o',str(target)],check=True)
 text=target.read_text()
 text='\n'.join(' * Generated from Noto Sans CJK SC; see assets/FONT-LICENSE.txt.' if line.startswith(' * Opts:') else line for line in text.splitlines())+'\n'
 if fallback=='radio_font_extra_20':text=text.replace('#ifndef RADIO_FONT_20','LV_FONT_DECLARE(radio_font_extra_20);\n\n#ifndef RADIO_FONT_20',1)
 target.write_text(text.rstrip()+'\n')
print('Generated two bounded common-Chinese fonts and frequency digits')
