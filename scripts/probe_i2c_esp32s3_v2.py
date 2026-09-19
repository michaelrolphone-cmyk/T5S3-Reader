#!/usr/bin/env python3
"""Cross-link the installable i2c.bus ELF against the private firmware ABI.

The temporary adapter implements the public provider capability and borrows
physical I2C0 operations from the firmware's single Wire owner. It MUST NOT
link an independent IDF I2C/GPIO/HAL/ISR implementation or reconfigure pins.
The dedicated private import is admitted only for i2c-esp32s3-v2.
"""
import json
from pathlib import Path
import re
import shlex
import subprocess

from probe_usb_controller_esp32s3 import ROOT, compile_target, tool

SOURCE = ROOT / 'Drivers/i2c_esp32s3_v2'
OUTPUT = ROOT / 'dist/experimental/i2c-esp32s3-v2'
BRIDGE = 'risc_fw_i2c_transact_v1'


def run():
    manifest = json.loads((SOURCE / 'manifest.json').read_text())
    if (manifest.get('id') != 'i2c-esp32s3-v2' or
            manifest.get('version') != '0.1.2' or
            manifest.get('driver_abi') != 2 or manifest.get('requires') != [] or
            manifest.get('provides') != [{'capability': 'i2c.bus', 'api': 1}] or
            manifest.get('status') != 'experimental-unpublished' or
            manifest.get('board') != 't5s3-pro'):
        raise RuntimeError('Firmware-backed I2C manifest mismatch')
    OUTPUT.mkdir(parents=True, exist_ok=True)
    subprocess.run(['pio', 'run', '-e', 't5s3-pro', '-t', 'compiledb'],
                   cwd=ROOT, check=True)
    entries = json.loads((ROOT / 'compile_commands.json').read_text())
    matched = [entry for entry in entries if
               Path(entry['file']).as_posix().endswith('src/native/NativeUsbBridge.cpp')]
    if len(matched) != 1:
        raise RuntimeError('Missing uniquely identified ESP32-S3 build configuration')
    entry = matched[0]
    args = list(entry['arguments']) if 'arguments' in entry else shlex.split(entry['command'])
    cc = Path(args[0]); nm = tool(cc, 'nm'); readelf = tool(cc, 'readelf')
    adapter = OUTPUT / 'adapter.o'
    compile_target(args, entry, SOURCE / 'driver.c', adapter,
                   c_compiler=True, extra=('-Wall', '-Wextra', '-Werror',
                                           '-ffunction-sections', '-fdata-sections'))
    output = OUTPUT / 'driver.elf'
    linker_layout = SOURCE / 'loader_sections.ld'
    if not linker_layout.is_file():
        raise RuntimeError('I2C runtime-loader linker layout is missing')
    subprocess.run([str(cc), '-shared', '-nostdlib', '-nostartfiles',
                    '-Wl,--hash-style=sysv', '-Wl,--exclude-libs,ALL',
                    '-Wl,-Bsymbolic', '-Wl,--gc-sections',
                    '-Wl,-T,' + str(linker_layout),
                    '-Wl,--version-script,' + str(SOURCE / 'exports.map'),
                    str(adapter), '-lgcc', '-o', str(output)], cwd=ROOT, check=True)
    undefined = subprocess.check_output([str(nm), '-u', str(output)], text=True)
    (OUTPUT / 'unresolved-symbols.txt').write_text(undefined)
    unresolved = {line.split()[-1] for line in undefined.splitlines() if line.split()}
    forbidden = sorted(name for name in unresolved if
                       name.startswith(('i2c_', 'gpio_', 'rtc_gpio_', 'rtc_io_',
                                        'periph_module_', 't5_', 'usb_')))
    if BRIDGE not in unresolved or forbidden:
        raise RuntimeError('I2C adapter must import only the private firmware transport, '
                           'not physical device routines: ' + repr(forbidden))
    symbols = subprocess.check_output([str(readelf), '--dyn-syms', '--wide',
                                       str(output)], text=True)
    exported = {p[7] for line in symbols.splitlines()
                if len(p := line.split()) >= 8 and p[3] == 'FUNC' and
                p[4] == 'GLOBAL' and p[6] != 'UND'}
    if exported != {'t5_driver_get'}:
        raise RuntimeError('I2C ELF exports unexpected entry points: ' + repr(exported))
    print('Installable i2c.bus ELF delegates physical I2C0 to firmware: PASS', flush=True)
    print('Firmware bridge import: ' + BRIDGE, flush=True)
    print('Independent I2C/GPIO/HAL/ISR implementations: none', flush=True)
    print('Firmware bus arbitration and hardware USB connection still require validation.',
          flush=True)


if __name__ == '__main__':
    run()
