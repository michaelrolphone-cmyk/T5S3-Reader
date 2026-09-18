#!/usr/bin/env python3
"""Set PR79 to the first unused firmware release and bump its three changed apps.

Version metadata is distinct from installation: this script only edits the
existing PR branch during source integration. It cannot tag or publish.
"""
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
platform = root / 'platformio.ini'
text = platform.read_text(encoding='utf-8')
old = '[riscrte]\nversion = 1.2.16\n'
new = '[riscrte]\nversion = 1.2.17\n'
if old in text:
    platform.write_text(text.replace(old, new, 1), encoding='utf-8')
elif new not in text:
    raise SystemExit('Unexpected firmware release version; refusing edit')

for filename, display, previous, target in (
    ('app_store.json', 'App Store', '1.0.1', '1.0.2'),
    ('driver_manager.json', 'Driver Manager', '1.0.1', '1.0.2'),
    ('serial_monitor.json', 'Serial Monitor', '1.2.0', '1.2.1'),
):
    path = root / 'Apps' / filename
    raw = path.read_text(encoding='utf-8')
    manifest = json.loads(raw)
    if manifest.get('display_name') != display or manifest.get('file_name') != filename[:-5] + '.elf':
        raise SystemExit(f'Unexpected app metadata: {path}')
    if manifest.get('version') not in (previous, target):
        raise SystemExit(f'Unexpected app version for {filename}')
    if manifest.get('min_firmware_version') not in ('1.2.4', '1.2.8', '1.2.9', '1.2.17'):
        raise SystemExit(f'Unexpected minimum firmware for {filename}')
    manifest['version'] = target
    manifest['min_firmware_version'] = '1.2.17'
    path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    print(f'{display}: {target} requires RiscRTE 1.2.17')
print('RiscRTE release version 1.2.17 prepared; no tag or release created')
