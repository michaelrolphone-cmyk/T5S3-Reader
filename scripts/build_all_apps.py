#!/usr/bin/env python3
"""Build every shipped ELF and its validated, source-controlled manifest."""
import pathlib
import subprocess
import sys

repo = pathlib.Path(__file__).resolve().parents[1]
outputs = set()
for source in sorted((repo / 'Apps').rglob('*.c')):
    name = str(source.relative_to(repo / 'Apps').with_suffix('')).replace('/', '__') + '.elf'
    if name in outputs:
        raise SystemExit(f'Duplicate release asset: {name}')
    outputs.add(name)
    output = repo / 'dist/apps' / name
    subprocess.run([sys.executable, str(repo / 'scripts/build_native_app.py'), str(source),
                    '--output', str(output), '--require-manifest'], check=True)
    subprocess.run(['bash', str(repo / 'test/run_native_app_test.sh'), str(output)], check=True)
if not outputs:
    raise SystemExit('No apps found')
