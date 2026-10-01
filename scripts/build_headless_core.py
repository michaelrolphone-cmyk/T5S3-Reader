#!/usr/bin/env python3
"""Build and freeze the core checkpoint using explicitly isolated PIO state."""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--core-dir', required=True, type=Path)
parser.add_argument('--out', required=True, type=Path, help='New evidence/artifact directory')
parser.add_argument('--pio', default='pio')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
if args.out.exists():
    parser.error('Output directory exists; choose a new path')
if args.core_dir.resolve() == (Path.home() / '.platformio').resolve():
    parser.error('Choose task-local PlatformIO state, not the existing global directory')
revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
if subprocess.check_output(['git', 'status', '--porcelain'], cwd=root, text=True).strip():
    parser.error('Commit the checkpoint first; frozen artifacts must identify a clean source revision')
env = os.environ.copy()
env.update(PLATFORMIO_CORE_DIR=str(args.core_dir.resolve()), PYTHONDONTWRITEBYTECODE='1',
           PLATFORMIO_SETTING_ENABLE_TELEMETRY='no', PLATFORMIO_SETTING_CHECK_PLATFORMIO_INTERVAL='0',
           PLATFORMIO_SETTING_CHECK_PLATFORMS_INTERVAL='0', PLATFORMIO_SETTING_CHECK_LIBRARIES_INTERVAL='0')
args.out.mkdir(parents=True)
with (args.out / 'build.log').open('x') as log:
    result = subprocess.run([args.pio, 'run', '-c', 'platformio.headless.ini'], cwd=root,
                            env=env, stdout=log, stderr=subprocess.STDOUT, timeout=900)
if result.returncode:
    raise SystemExit(f'Build failed ({result.returncode}); see {args.out / "build.log"}')
if subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip() != revision or \
        subprocess.check_output(['git', 'status', '--porcelain'], cwd=root, text=True).strip():
    raise SystemExit('Source changed during build; refusing to freeze artifacts')
hashes = {}
for name in ('firmware.bin', 'firmware.elf'):
    target = args.out / name
    shutil.copy2(root / '.pio/build/esp32s3-core-check' / name, target)
    hashes[name] = hashlib.sha256(target.read_bytes()).hexdigest()
manifest = dict(revision=revision, environment='esp32s3-core-check', sha256=hashes,
                app_offset='0x10000', flash='16MB QIO', psram='OPI', uart_baud=115200,
                scope='core primitives checkpoint; no packages/provisioning/camera')
(args.out / 'build-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(json.dumps(manifest, indent=2))
