#!/usr/bin/env python3
"""Cross-build actual BQ25896 board.power.vbus driver; no firmware charger proxy."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from native_app_symbols import firmware_exports, validate_imports

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'Drivers/board_power_t5s3_v2'
OUTPUT = ROOT / 'dist/experimental/usb-board-power-t5s3-v2'


def build(cc=None):
    manifest = json.loads((SOURCE / 'manifest.json').read_text())
    required = {
        'type': 'driver', 'id': 'board-power-t5s3-v2', 'driver_abi': 2,
        'architecture': 'xtensa-esp32s3', 'file_name': 'driver.elf',
        'requires': [{'capability': 'i2c.bus', 'api': 1},
                     {'capability': 'platform.clock', 'api': 1}],
        'provides': [{'capability': 'board.power.vbus', 'api': 1}],
        'status': 'experimental-unpublished', 'board': 't5s3-pro',
    }
    if any(manifest.get(key) != value for key, value in required.items()):
        raise ValueError('Invalid board power package dependencies or identity')
    cc = cc or os.environ.get('NATIVE_DRIVER_CC') or shutil.which('xtensa-esp32s3-elf-gcc')
    if not cc:
        core = Path(os.environ.get('PLATFORMIO_CORE_DIR', Path.home() / '.platformio'))
        cc = str(core / 'packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc')
    OUTPUT.mkdir(parents=True, exist_ok=True)
    elf = OUTPUT / 'driver.elf'
    subprocess.run([
        cc, '-std=c11', '-Os', '-fPIC', '-mtext-section-literals', '-mlongcalls',
        '-fvisibility=hidden', '-nostdlib', '-nostartfiles', '-shared',
        '-I' + str(ROOT / 'sdk/driver'), '-Wl,--hash-style=sysv',
        '-Wl,--exclude-libs,ALL', str(SOURCE / 'driver.c'), '-lgcc',
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
    (OUTPUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Unpublished physical BQ25896 VBUS ELF built; I2C/clock dependencies '
          'and runtime hardware-ownership cutover are still required')
    return elf


if __name__ == '__main__':
    build()
