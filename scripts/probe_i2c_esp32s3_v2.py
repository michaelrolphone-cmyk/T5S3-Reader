#!/usr/bin/env python3
"""Cross-link actual ESP32-S3 I2C, GPIO, HAL and ISR code inside its ELF.

Never resolve peripheral i2c_*/gpio_* calls from compiled resident firmware.
The provider is experimental until all loader imports and exclusive physical
I2C0/legacy Wire arbitration are validated.
"""
import json
from pathlib import Path
import re
import shlex
import subprocess

from probe_usb_controller_esp32s3 import (
    ROOT, SOURCE_CACHE, IDF_TAG, compile_target, idf_sources, tool)

SOURCE = ROOT / 'Drivers/i2c_esp32s3_v2'
OUTPUT = ROOT / 'dist/experimental/i2c-esp32s3-v2'
SOURCES = (
    'components/driver/i2c.c',
    'components/driver/gpio.c',
    'components/driver/periph_ctrl.c',
    'components/hal/i2c_hal.c',
    # ESP-IDF defines interrupt-safe HAL handlers in their own translation unit.
    # Leaving this out would route actual I2C interrupt work into firmware.
    'components/hal/i2c_hal_iram.c',
    'components/hal/gpio_hal.c',
    'components/soc/esp32s3/i2c_periph.c',
    'components/soc/esp32s3/gpio_periph.c',
)
MMIO = ('I2C0', 'I2C1', 'GPIO', 'SYSTEM', 'RTCCNTL', 'USB_SERIAL_JTAG')
# GPIO has additional RTC/sleep operations irrelevant to the I2C host bus.
# Build independent PIC sections and collect only reachable functions/data;
# pulling those unused paths in via a whole object creates false runtime
# dependencies on another peripheral driver's RTC implementation.
FUNCTION_SECTIONS = ('-ffunction-sections', '-fdata-sections')


def peripheral_map():
    source = SOURCE_CACHE / 'components/soc/esp32s3/ld/esp32s3.peripherals.ld'
    values = dict(re.findall(r'PROVIDE\s*\(\s*(\w+)\s*=\s*(0x[0-9a-fA-F]+)\s*\)',
                             source.read_text()))
    if not all(name in values for name in MMIO):
        raise RuntimeError('Pinned ESP32-S3 peripheral map missing I2C/GPIO registers')
    return [f'-Wl,--defsym,{name}={values[name]}' for name in MMIO]


def run():
    manifest = json.loads((SOURCE / 'manifest.json').read_text())
    if (manifest.get('id') != 'i2c-esp32s3-v2' or
            manifest.get('driver_abi') != 2 or manifest.get('requires') != [] or
            manifest.get('provides') != [{'capability': 'i2c.bus', 'api': 1}] or
            manifest.get('status') != 'experimental-unpublished' or
            manifest.get('board') != 't5s3-pro'):
        raise RuntimeError('Physical I2C manifest mismatch')
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
    idf_sources()
    subprocess.run(['git', '-C', str(SOURCE_CACHE), 'sparse-checkout', 'add',
                    'components/driver'], check=True)
    sha = subprocess.check_output(['git', '-C', str(SOURCE_CACHE), 'rev-parse',
                                   'HEAD'], text=True).strip()
    (OUTPUT / 'idf-i2c-source.txt').write_text(f'{IDF_TAG} {sha}\n')
    includes = tuple('-I' + str(SOURCE_CACHE / folder) for folder in (
        'components/driver/include', 'components/hal/include',
        'components/hal/esp32s3/include', 'components/soc/esp32s3',
        'components/soc/esp32s3/include'))
    objects = []
    controller = OUTPUT / 'controller.o'
    compile_target(args, entry, SOURCE / 'driver.c', controller,
                   c_compiler=True,
                   extra=includes + FUNCTION_SECTIONS + ('-Wall', '-Wextra', '-Werror'))
    objects.append(controller)
    undefined = subprocess.check_output([str(nm), '-u', str(controller)], text=True)
    if 'i2c_master_write_read_device' not in undefined or 'i2c_driver_install' not in undefined:
        raise RuntimeError('I2C provider source does not perform actual IDF hardware I/O')
    for index, filename in enumerate(SOURCES):
        source = SOURCE_CACHE / filename
        if not source.is_file():
            raise RuntimeError('Pinned IDF I2C dependency absent: ' + filename)
        object_file = OUTPUT / f'idf-i2c-{index}.o'
        compile_target(args, entry, source, object_file,
                       c_compiler=True, extra=includes + FUNCTION_SECTIONS)
        objects.append(object_file)
    output = OUTPUT / 'driver.elf'
    subprocess.run([str(cc), '-shared', '-nostdlib', '-nostartfiles',
                    '-Wl,--hash-style=sysv', '-Wl,--exclude-libs,ALL',
                    '-Wl,-Bsymbolic', '-Wl,--gc-sections',
                    '-Wl,--version-script,' + str(SOURCE / 'exports.map'),
                    *peripheral_map(), *map(str, objects), '-lgcc',
                    '-o', str(output)], cwd=ROOT, check=True)
    undefined = subprocess.check_output([str(nm), '-u', str(output)], text=True)
    (OUTPUT / 'unresolved-symbols.txt').write_text(undefined)
    # This must match *every* physical function the provider needs. Unknown
    # peripheral imports are not acceptable generic CPU/OS services.
    forbidden = [line for line in undefined.splitlines() if
                 re.search(r'\b(?:i2c_|gpio_|rtc_gpio_|rtc_io_|periph_module_)', line)]
    if forbidden:
        raise RuntimeError('I2C ELF still imports a resident peripheral implementation: '
                           + ', '.join(forbidden))
    symbols = subprocess.check_output([str(readelf), '--dyn-syms', '--wide',
                                       str(output)], text=True)
    exported = {p[7] for line in symbols.splitlines()
                if len(p := line.split()) >= 8 and p[3] == 'FUNC' and
                p[4] == 'GLOBAL' and p[6] != 'UND'}
    if exported != {'t5_driver_get'}:
        raise RuntimeError('I2C ELF exports unexpected entry points: ' + repr(exported))
    print('Physical ESP32-S3 I2C ELF + PIC IDF controller/GPIO/ISR HAL linked: PASS',
          flush=True)
    print('Remaining imported generic platform services:',
          len(undefined.splitlines()), flush=True)
    print('Exclusive legacy Wire arbitration and loader ABI remain release blockers.',
          flush=True)


if __name__ == '__main__':
    run()
