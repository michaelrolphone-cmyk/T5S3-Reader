#!/usr/bin/env python3
"""Build reusable BQ25896 ELF and separate T5S3 profile (legacy command name)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from native_app_symbols import firmware_exports, validate_imports

ROOT = Path(__file__).resolve().parents[1]


def build_provider(source, output, identity, dependencies, capability, cc=None):
    manifest = json.loads((source / 'manifest.json').read_text())
    required = {
        'type': 'driver', 'id': identity, 'driver_abi': 2,
        'architecture': 'xtensa-esp32s3', 'file_name': 'driver.elf',
        'requires': [{'capability': dep, 'api': 1} for dep in dependencies],
        'provides': [{'capability': capability, 'api': 1}],
        'status': 'experimental-unpublished',
    }
    if any(manifest.get(key) != value for key, value in required.items()):
        raise ValueError('Invalid board power package dependencies or identity')
    cc = cc or os.environ.get('NATIVE_DRIVER_CC') or shutil.which('xtensa-esp32s3-elf-gcc')
    if not cc:
        core = Path(os.environ.get('PLATFORMIO_CORE_DIR', Path.home() / '.platformio'))
        cc = str(core / 'packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc')
    output.mkdir(parents=True, exist_ok=True)
    elf = output / 'driver.elf'
    subprocess.run([
        cc, '-std=c11', '-Os', '-fPIC', '-mtext-section-literals', '-mlongcalls',
        '-fvisibility=hidden', '-nostdlib', '-nostartfiles', '-shared',
        '-I' + str(ROOT / 'sdk/driver'), '-Wl,--hash-style=sysv',
        '-Wl,--exclude-libs,ALL', str(source / 'driver.c'), '-lgcc',
        '-o', str(elf),
    ], check=True)
    readelf = str(Path(cc).with_name(Path(cc).name.replace('gcc', 'readelf')))
    symbols = subprocess.check_output([readelf, '--dyn-syms', '--wide', str(elf)], text=True)
    exported = {parts[7] for line in symbols.splitlines()
                if len(parts := line.split()) >= 8 and parts[4] == 'GLOBAL'
                and parts[6] != 'UND' and parts[3] == 'FUNC'}
    if exported != {'t5_driver_get'}:
        raise ValueError('Board power must export only the generic root')
    validate_imports(symbols, {symbol for symbol in firmware_exports(ROOT)
                               if not symbol.startswith('t5_')})
    data = elf.read_bytes()
    if not 52 <= len(data) <= 256 * 1024 or data[:7] != b'\x7fELF\x01\x01\x01' or \
       int.from_bytes(data[16:18], 'little') != 3 or \
       int.from_bytes(data[18:20], 'little') != 94:
        raise ValueError('Invalid Xtensa board-power ELF')
    manifest.update(size_bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Power provider ELF built: {identity} -> {elf}')
    return elf


def build(cc=None, identities=None):
    # The profile and chip driver are separate packages. Build only the IDs
    # selected by the release plan.
    valid = {'t5s3-usb-power-profile', 'board-power-t5s3-v2'}
    requested = set(identities) if identities else valid
    if not requested or not requested <= valid:
        raise ValueError(f'Invalid board power package IDs: {sorted(requested - valid)}')
    results = []
    if 't5s3-usb-power-profile' in requested:
        results.append(build_provider(ROOT / 'Drivers/t5s3_usb_power_profile',
                       ROOT / 'dist/experimental/t5s3-usb-power-profile',
                       't5s3-usb-power-profile', (), 'board.power.bq25896.profile', cc))
    if 'board-power-t5s3-v2' in requested:
        results.append(build_provider(ROOT / 'Drivers/bq25896',
                          ROOT / 'dist/experimental/usb-board-power-t5s3-v2',
                          'board-power-t5s3-v2',
                          ('i2c.bus', 'platform.clock', 'board.power.bq25896.profile'), 
                          'board.power.vbus', cc))
    return results


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ids', nargs='+', help='build only these board power package IDs')
    args = parser.parse_args()
    build(identities=args.ids)
