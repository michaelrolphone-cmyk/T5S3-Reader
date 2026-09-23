#!/usr/bin/env python3
"""Build an independently installable HID or XInput class ELF."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

from native_app_symbols import firmware_exports, validate_imports

ROOT = Path(__file__).resolve().parents[1]


def build(source_name: str, identity: str, dependency: str | tuple[str, ...], capability: str,
          cc: str | None = None) -> Path:
    if source_name not in ('usb_hid', 'usb_hid_keyboard', 'usb_hid_gamepad',
                           'usb_xinput_gamepad', 'usb_ui_navigation'):
        raise ValueError('invalid source driver')
    source = ROOT / 'Drivers' / source_name
    manifest = json.loads((source / 'manifest.json').read_text(encoding='utf-8'))
    dependencies = (dependency,) if isinstance(dependency, str) else dependency
    expected = {
        'type': 'driver', 'id': identity, 'driver_abi': 2,
        'architecture': 'xtensa-esp32s3', 'file_name': 'driver.elf',
        'requires': [{'capability': item, 'api': 1} for item in dependencies],
        'provides': [{'capability': capability, 'api': 1}],
        'status': 'experimental-unpublished',
    }
    if any(manifest.get(k) != v for k, v in expected.items()) or not isinstance(manifest.get('version'), str):
        raise ValueError(f'invalid input class source manifest {source}')
    cc = cc or os.environ.get('NATIVE_DRIVER_CC') or shutil.which('xtensa-esp32s3-elf-gcc')
    if not cc:
        core = Path(os.environ.get('PLATFORMIO_CORE_DIR', Path.home() / '.platformio'))
        cc = str(core / 'packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc')
    output = ROOT / 'dist/experimental' / identity
    output.mkdir(parents=True, exist_ok=True)
    elf = output / 'driver.elf'
    subprocess.run([
        str(cc), '-std=c11', '-Os', '-fPIC', '-mtext-section-literals',
        '-mlongcalls', '-fvisibility=hidden', '-nostdlib', '-nostartfiles',
        '-shared', '-I' + str(ROOT / 'sdk/driver'), '-Wl,--hash-style=sysv',
        '-Wl,--exclude-libs,ALL', str(source / 'driver.c'), '-lgcc',
        '-o', str(elf),
    ], cwd=ROOT, check=True)
    readelf = str(Path(cc).with_name(Path(cc).name.replace('gcc', 'readelf')))
    symbols = subprocess.check_output([readelf, '--dyn-syms', '--wide', str(elf)], text=True)
    exports = {f[7] for line in symbols.splitlines()
               if len(f := line.split()) >= 8 and f[4] == 'GLOBAL'
               and f[6] != 'UND' and f[3] == 'FUNC'}
    if exports != {'t5_driver_get'}:
        raise ValueError(f'{identity}: driver must export only t5_driver_get: {exports}')
    validate_imports(symbols, {symbol for symbol in firmware_exports(ROOT)
                               if not symbol.startswith('t5_')})
    payload = elf.read_bytes()
    if not 52 <= len(payload) <= 256 * 1024 or payload[:7] != b'\x7fELF\x01\x01\x01' or \
       int.from_bytes(payload[16:18], 'little') != 3 or \
       int.from_bytes(payload[18:20], 'little') != 94:
        raise ValueError(f'{identity}: invalid Xtensa shared ELF')
    manifest.update(size_bytes=len(payload), sha256=hashlib.sha256(payload).hexdigest())
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n',
                                          encoding='utf-8')
    print(f'USB input class ELF built: {identity} -> {elf}')
    return elf
