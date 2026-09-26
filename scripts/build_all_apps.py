#!/usr/bin/env python3
"""Build shipped ELFs and stage every app as an ordinary package.

The legacy aggregate app catalog is retained temporarily for older firmware,
but U1 release publication discovers applications from dist/packages alongside
drivers/services/providers and emits one immutable .rte.zip per package.
"""
import hashlib
import argparse
import json
import pathlib
import shutil
import subprocess
import sys

from package_integrity import stamp_app_manifest

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--id", help="Build one stable app ID instead of every app")
args = parser.parse_args()

repo = pathlib.Path(__file__).resolve().parents[1]
apps_out = repo / 'dist/apps'
packages_out = repo / 'dist/packages'
apps_out.mkdir(parents=True, exist_ok=True)
packages_out.mkdir(parents=True, exist_ok=True)
outputs = set()
catalog = []


def entry(path: pathlib.Path, executable: bool) -> dict:
    payload = path.read_bytes()
    if not payload:
        raise SystemExit(f'Empty ordinary package entry: {path}')
    return {
        'name': path.name,
        'size_bytes': len(payload),
        'sha256': hashlib.sha256(payload).hexdigest(),
        'executable': executable,
    }


for source in sorted((repo / 'Apps').rglob('*.c')):
    name = str(source.relative_to(repo / 'Apps').with_suffix('')).replace('/', '__') + '.elf'
    if args.id and name != args.id + '.elf':
        continue
    if name in outputs:
        raise SystemExit(f'Duplicate release asset: {name}')
    outputs.add(name)
    output = apps_out / name
    subprocess.run([sys.executable, str(repo / 'scripts/build_native_app.py'), str(source),
                    '--output', str(output), '--require-manifest'], check=True)
    subprocess.run(['bash', str(repo / 'test/run_native_app_test.sh'), str(output)], check=True)
    manifest_path = output.with_suffix('.json')
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    manifest = stamp_app_manifest(manifest, output)
    manifest_path.write_text(json.dumps(manifest, separators=(',', ':'), ensure_ascii=False) + '\n',
                             encoding='utf-8')
    if stamp_app_manifest(json.loads(manifest_path.read_text(encoding='utf-8')), output) != manifest:
        raise SystemExit(f'Published ELF digest mismatch for {name}')

    version = manifest.get('version')
    file_name = manifest.get('file_name')
    if file_name != name or not isinstance(version, str):
        raise SystemExit(f'App manifest identity mismatch for {name}')
    package_id = name[:-4]
    package = packages_out / package_id
    if package.exists():
        if not package.is_dir():
            raise SystemExit(f'Ordinary package target is not a directory: {package}')
        shutil.rmtree(package)
    package.mkdir()
    package_elf = package / name
    package_sidecar = package / manifest_path.name
    shutil.copyfile(output, package_elf)
    shutil.copyfile(manifest_path, package_sidecar)
    ordinary = {
        'schema': 1,
        'kind': 'application',
        'id': package_id,
        'version': version,
        'artifact': name,
        'architecture': 'xtensa-esp32s3',
        'min_runtime_api': 2,
        'entries': [entry(package_elf, True), entry(package_sidecar, False)],
        'requires': [],
    }
    encoded = (json.dumps(ordinary, separators=(',', ':'), ensure_ascii=True) + '\n').encode('ascii')
    if len(encoded) > 4096:
        raise SystemExit(f'Ordinary app manifest exceeds device parser bound: {package_id}')
    (package / '.package.json').write_bytes(encoded)
    catalog.append(manifest)

if not outputs:
    raise SystemExit(f"No app found for ID {args.id!r}" if args.id else 'No apps found')

catalog.sort(key=lambda item: (item['display_name'].casefold(), item['file_name']))
encoded = json.dumps({'schema': 1, 'apps': catalog}, separators=(',', ':')) + '\n'
if len(catalog) > 128 or len(encoded.encode('utf-8')) > 64 * 1024:
    raise SystemExit('Release catalog exceeds on-device parser budget')
(apps_out / 'app-catalog.json').write_text(encoded, encoding='utf-8')
print(f'Published aggregate compatibility catalog and staged {len(catalog)} ordinary application packages')
