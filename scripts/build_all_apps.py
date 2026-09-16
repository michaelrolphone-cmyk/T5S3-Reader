#!/usr/bin/env python3
"""Build every shipped ELF and publish a compact aggregate app catalog."""
import json
import pathlib
import subprocess
import sys

repo = pathlib.Path(__file__).resolve().parents[1]
outputs = set()
catalog = []
for source in sorted((repo / 'Apps').rglob('*.c')):
    name = str(source.relative_to(repo / 'Apps').with_suffix('')).replace('/', '__') + '.elf'
    if name in outputs:
        raise SystemExit(f'Duplicate release asset: {name}')
    outputs.add(name)
    output = repo / 'dist/apps' / name
    subprocess.run([sys.executable, str(repo / 'scripts/build_native_app.py'), str(source),
                    '--output', str(output), '--require-manifest'], check=True)
    subprocess.run(['bash', str(repo / 'test/run_native_app_test.sh'), str(output)], check=True)
    manifest = json.loads(output.with_suffix('.json').read_text(encoding='utf-8'))
    if manifest.get('file_name') != name:
        raise SystemExit(f'Published manifest filename mismatch for {name}')
    catalog.append(manifest)
if not outputs:
    raise SystemExit('No apps found')

catalog.sort(key=lambda item: (item['display_name'].casefold(), item['file_name']))
(repo / 'dist/apps/app-catalog.json').write_text(
    json.dumps({'schema': 1, 'apps': catalog}, separators=(',', ':')) + '\n',
    encoding='utf-8')
print(f'Published aggregate app catalog with {len(catalog)} entries')
