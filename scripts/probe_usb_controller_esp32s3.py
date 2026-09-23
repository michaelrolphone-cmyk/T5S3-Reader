#!/usr/bin/env python3
"""Build ESP32-S3 usb.controller with its own PIC IDF USB, PHY HAL and SOC data.

The bundled libusb.a is statically linked for resident firmware and triggers
Xtensa dangerous dynamic relocations when linked directly into an ELF. Rebuild
only USB-owned sources from the pinned IDF release as PIC. Hardware register
addresses come from that same pinned IDF's SoC linker definitions; they are
bound inside the provider rather than imported from resident firmware.
Loader/import compatibility and on-board use remain separate verification steps.
"""
import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess
from instrument_usb_enumeration import instrument

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'Drivers/usb_controller_esp32s3/driver.cpp'
PHY_GPIO_SOURCE = ROOT / 'Drivers/usb_controller_esp32s3/phy_gpio.c'
OUTPUT = ROOT / 'dist/experimental/usb-controller-esp32s3'
SOURCE_CACHE = ROOT / 'dist/idf-usb-source/v4.4.7'
IDF_TAG = 'v4.4.7'
USB_SOURCES = ('hcd_dwc.c', 'hub.c', 'usb_helpers.c', 'usb_host.c',
               'usb_private.c', 'usbh.c', 'usb_phy.c')
USB_HAL_SOURCES = ('usb_hal.c', 'usb_phy_hal.c', 'usb_dwc_hal.c')
USB_SOC_SOURCES = ('usb_phy_periph.c', 'usb_periph.c', 'gpio_periph.c')
# These are memory-mapped hardware registers, not resident symbols or objects.
# The provider's PIC code must reference the exact physical addresses from the
# pinned target SoC linker script. Do not fabricate C data objects at them.
MMIO_SYMBOLS = ('GPIO', 'RTCCNTL', 'SYSTEM', 'USB_DWC', 'USB_SERIAL_JTAG', 'USB_WRAP')
EXPORT_MAP = ROOT / 'Drivers/usb_controller_esp32s3/exports.map'


def tool(compiler, suffix):
    name = compiler.name
    if not name.endswith(('g++', 'gcc')):
        raise RuntimeError('Unexpected Xtensa compiler: ' + str(compiler))
    return compiler.with_name(name[:-3] + suffix)


def compile_target(argv, entry, source, output, c_compiler=False, extra=()):
    # PlatformIO can add this newer GCC flag to the compilation database.
    # The pinned Xtensa GCC 8 toolchain does not recognize it in C or C++.
    command = [arg for arg in argv if arg != '-Wno-bidi-chars']
    changed_source = changed_output = False
    for i, argument in enumerate(command):
        if Path(argument).as_posix().endswith('src/native/NativeUsbBridge.cpp'):
            command[i] = str(source)
            changed_source = True
        elif i and command[i - 1] == '-o':
            command[i] = str(output)
            changed_output = True
    if not changed_source or not changed_output or '-c' not in command:
        raise RuntimeError('Could not derive exact ESP32-S3 compilation configuration')
    if c_compiler:
        compiler = Path(command[0])
        if not compiler.name.endswith('g++'):
            raise RuntimeError('Expected Xtensa C++ toolchain to derive C compiler')
        command[0] = str(tool(compiler, 'gcc'))
        command = [arg for arg in command if not arg.startswith('-std=') and
                   arg != '-fno-rtti']
        command.append('-std=gnu11')
    command.extend(['-fPIC', '-fvisibility=hidden', '-I' + str(ROOT / 'sdk/driver')])
    command.extend(extra)
    subprocess.run(command, cwd=entry.get('directory', str(ROOT)), check=True)
    if not output.is_file() or not output.stat().st_size:
        raise RuntimeError('Target compiler did not produce ' + str(output))


def idf_sources():
    checkout = SOURCE_CACHE
    if not (checkout / 'components/usb/usb_host.c').is_file():
        if checkout.exists():
            raise RuntimeError('Incomplete IDF source checkout: ' + str(checkout))
        checkout.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(['git', 'clone', '--depth=1', '--branch', IDF_TAG,
                        '--filter=blob:none', '--sparse',
                        'https://github.com/espressif/esp-idf.git', str(checkout)],
                       cwd=ROOT, check=True)
    subprocess.run(['git', '-C', str(checkout), 'sparse-checkout', 'set',
                    'components/usb', 'components/hal', 'components/soc/esp32s3'],
                   check=True)
    commit = subprocess.check_output(['git', '-C', str(checkout), 'rev-parse',
                                      'HEAD'], text=True).strip()
    (OUTPUT / 'idf-usb-source.txt').write_text(f'{IDF_TAG} {commit}\n')
    usb = checkout / 'components/usb'
    hal = checkout / 'components/hal'
    soc = checkout / 'components/soc/esp32s3'
    paths = ([usb / name for name in USB_SOURCES] +
             [hal / name for name in USB_HAL_SOURCES] +
             [soc / name for name in USB_SOC_SOURCES])
    if any(not path.is_file() for path in paths):
        raise RuntimeError('Pinned IDF USB/PHY/SOC source set is incomplete')
    return usb, hal, soc, paths


def target_mmio_symbols(soc):
    """Read actual ESP32-S3 memory map, never hard-code host-firmware globals."""
    linker = soc / 'ld/esp32s3.peripherals.ld'
    if not linker.is_file() or not EXPORT_MAP.is_file():
        raise FileNotFoundError('Pinned physical peripheral map or ELF export policy absent')
    matches = re.findall(r'PROVIDE\s*\(\s*(\w+)\s*=\s*(0x[0-9a-fA-F]+)\s*\)',
                         linker.read_text())
    values = dict(matches)
    if len(matches) != len(values) or any(name not in values for name in MMIO_SYMBOLS):
        raise RuntimeError('Pinned ESP32-S3 peripheral map is incomplete or ambiguous')
    return [f'-Wl,--defsym,{name}={values[name]}' for name in MMIO_SYMBOLS]


def run():
    parser = argparse.ArgumentParser()
    parser.add_argument('--link-experiment', action='store_true')
    args = parser.parse_args()
    OUTPUT.mkdir(parents=True, exist_ok=True)
    subprocess.run(['pio', 'run', '-e', 't5s3-pro', '-t', 'compiledb'],
                   cwd=ROOT, check=True)
    entries = json.loads((ROOT / 'compile_commands.json').read_text())
    matching = [entry for entry in entries if
                Path(entry['file']).as_posix().endswith('src/native/NativeUsbBridge.cpp')]
    if len(matching) != 1:
        raise RuntimeError('Expected exactly one ESP32-S3 USB compilation command')
    entry = matching[0]
    argv = list(entry['arguments']) if 'arguments' in entry else shlex.split(entry['command'])
    controller = OUTPUT / 'controller.o'
    compile_target(argv, entry, SOURCE, controller, extra=('-Wall', '-Wextra', '-Werror'))
    compiler = Path(argv[0])
    nm = tool(compiler, 'nm')
    undefined = subprocess.check_output([str(nm), '-u', str(controller)], text=True)
    if 'usb_host_install' not in undefined or 'usb_host_transfer_submit' not in undefined:
        raise RuntimeError('Controller does not invoke real IDF USB hardware implementation')
    if any(name in undefined for name in ('nativeUsb', 'NativeUsbBridge', 't5_usb_')):
        raise RuntimeError('Controller imports the legacy compiled firmware USB path')
    print('Physical controller compiled against ESP32-S3 IDF: PASS', flush=True)
    if not args.link_experiment:
        print('Link, import and relocation audit requires --link-experiment', flush=True)
        return

    usb, hal, soc, paths = idf_sources()
    includes = ('-I' + str(usb / 'include'), '-I' + str(usb / 'private_include'),
                '-I' + str(hal / 'include'), '-I' + str(hal / 'esp32s3/include'),
                '-I' + str(soc), '-I' + str(soc / 'include'))
    objects = [controller]
    for index, path in enumerate(paths):
        obj = OUTPUT / f'idf-usb-{index}-{path.name}.o'
        if path.name == 'hub.c':
            staged_hub = OUTPUT / 'hub-enumeration-diagnostics.c'
            staged_hub.write_text(instrument(path.read_text()))
            path = staged_hub
        compile_target(argv, entry, path, obj, c_compiler=True, extra=includes)
        objects.append(obj)
    phy_gpio = OUTPUT / 'phy-gpio.o'
    compile_target(argv, entry, PHY_GPIO_SOURCE, phy_gpio, c_compiler=True,
                   extra=(*includes, '-Wall', '-Wextra', '-Werror'))
    objects.append(phy_gpio)
    elf = OUTPUT / 'controller-link-experiment.elf'
    command = [str(compiler), '-shared', '-nostdlib', '-nostartfiles',
               '-Wl,--hash-style=sysv', '-Wl,--exclude-libs,ALL',
               '-Wl,-Bsymbolic', '-Wl,--version-script,' + str(EXPORT_MAP),
               *target_mmio_symbols(soc), *map(str, objects), '-lgcc', '-o', str(elf)]
    subprocess.run(command, cwd=ROOT, check=True)
    imported = subprocess.check_output([str(nm), '-u', str(elf)], text=True)
    (OUTPUT / 'unresolved-symbols.txt').write_text(imported)
    namespaces = ('usb_host_', 'usbh_', 'hcd_', 'hub_', 'usb_phy_',
                  'usb_new_phy', 'urb_', 'risc_usb_enum_')
    offenders = [line.strip() for line in imported.splitlines()
                 if any(namespace in line for namespace in namespaces)]
    if offenders:
        print('Unresolved USB internals:', *offenders, sep='\n  ', flush=True)
        raise RuntimeError('Physical controller still imports internal USB implementation')
    if 'gpio_set_drive_capability' in imported:
        raise RuntimeError('USB PHY GPIO pad configuration leaked back into firmware')
    readelf = tool(compiler, 'readelf')
    symbols = subprocess.check_output([str(readelf), '--dyn-syms', '--wide', str(elf)],
                                      text=True)
    export_functions = {parts[7] for line in symbols.splitlines()
                        if len(parts := line.split()) >= 8 and parts[3] == 'FUNC'
                        and parts[4] == 'GLOBAL' and parts[6] != 'UND'}
    if export_functions != {'t5_driver_get'}:
        raise RuntimeError('Unexpected exported ELF functions: ' + repr(export_functions))
    defined = subprocess.check_output([str(nm), '-D', '--defined-only', str(elf)], text=True)
    extra_symbols = {line.split()[-1] for line in defined.splitlines()} - {
        't5_driver_get', '__bss_start', '_edata', '_end'}
    if extra_symbols:
        raise RuntimeError('Unexpected exported ELF data: ' + repr(extra_symbols))
    dynamic = subprocess.check_output([str(readelf), '-d', str(elf)], text=True)
    if 'TEXTREL' in dynamic:
        raise RuntimeError('Controller contains text/read-only dynamic relocations')
    print('Physical controller + PIC-built IDF USB/PHY/SOC + owned PHY GPIO linked: PASS', flush=True)
    print('Unresolved generic OS/CPU imports and loader/board-power path still require validation.', flush=True)


if __name__ == '__main__':
    run()
