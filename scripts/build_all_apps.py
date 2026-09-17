#!/usr/bin/env python3
"""Build every shipped ELF and publish a compact aggregate app catalog."""
import json
import pathlib
import subprocess
import sys
from package_integrity import stamp_app_manifest

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
    manifest_path = output.with_suffix('.json')
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    manifest = stamp_app_manifest(manifest, output)
    manifest_path.write_text(json.dumps(manifest, separators=(',', ':'), ensure_ascii=False) + '\n',
                             encoding='utf-8')
    # Check the published pair after writing; fail the release build if the
    # bytes changed or the sidecar no longer agrees with the ELF.
    if stamp_app_manifest(json.loads(manifest_path.read_text(encoding='utf-8')), output) != manifest:
        raise SystemExit(f'Published ELF digest mismatch for {name}')
    catalog.append(manifest)
if not outputs:
    raise SystemExit('No apps found')

catalog.sort(key=lambda item: (item['display_name'].casefold(), item['file_name']))
encoded = json.dumps({'schema': 1, 'apps': catalog}, separators=(',', ':')) + '\n'
# src/native/AppCatalogIndex.cpp accepts at most 64 KiB / 128 entries.
if len(catalog) > 128 or len(encoded.encode('utf-8')) > 64 * 1024:
    raise SystemExit('Release catalog exceeds on-device parser budget')
(repo / 'dist/apps/app-catalog.json').write_text(encoded, encoding='utf-8')
print(f'Published aggregate app catalog with {len(catalog)} entries and ELF SHA-256 digests')
