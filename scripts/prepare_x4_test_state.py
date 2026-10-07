#!/usr/bin/env python3
"""Prepare local test-state copies; never access or replace a device filesystem."""
import argparse,json
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--pins',type=Path,help='Previously captured /Apps/.home_apps')
p.add_argument('--settings',type=Path,help='Previously captured settings.json')
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=False)
pins=a.pins.read_text() if a.pins else ''
if len(pins.encode())>16384: raise SystemExit('Pin file exceeds device bound')
items=pins.splitlines()
for name in ('driver_manager.elf','app_store.elf'):
 if name not in items: items.append(name)
if len(items)>128: raise SystemExit('Pin count exceeds device bound')
(a.output/'.home_apps').write_text('\n'.join(items)+'\n')
if a.settings:
 settings=json.loads(a.settings.read_text())
 if not isinstance(settings,dict): raise SystemExit('Expected existing settings object')
 settings['backlightLevel']=2 # owner requested visible light for this manual test
 (a.output/'settings.json').write_text(json.dumps(settings,separators=(',',':'))+'\n')
else:
 (a.output/'settings-patch.json').write_text('{"backlightLevel":2}\n')
 (a.output/'README.txt').write_text('Merge backlightLevel=2 into existing settings; do not replace settings.json with this patch. .home_apps is newline-separated ELF basenames, not JSON. Generated from no prior pins unless --pins was supplied. Capture current state before device staging.\n')
