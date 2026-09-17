#!/usr/bin/env python3
"""Build ESP32-S3 usb.controller with its OWN position-independent IDF USB stack.

The Arduino 2.0.17 prebuilt libusb.a is firmware-oriented and its read-only
pointer tables cannot safely be dynamically relocated. Compile the matching
ESP-IDF v4.4.7 USB component from source instead. The compiled core exports
NO USB entry points. This experimental ELF is NOT an installable package until
its OS imports, relocations, board power dependency and loader are validated.
"""
import argparse
import json
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'Drivers/usb_controller_esp32s3/driver.cpp'
OUTPUT = ROOT / 'dist/experimental/usb-controller-esp32s3'
IDF_TAG = 'v4.4.7'  # Arduino-ESP32 2.0.17's ESP-IDF release.
USB_SOURCES = ('hcd_dwc.c', 'hub.c', 'usb_helpers.c', 'usb_host.c',
               'usb_private.c', 'usbh.c', 'usb_phy.c')


def tool(compiler, suffix):
    name = compiler.name
    if not name.endswith(('g++', 'gcc')):
        raise RuntimeError('Unexpected target compiler: ' + str(compiler))
    return compiler.with_name(name[:-3] + suffix)


def compile_target(argv, entry, source, output, c_compiler=False, extra=()):
    command = list(argv)
    changed_source = changed_output = False
    for i, argument in enumerate(command):
        if Path(argument).as_posix().endswith('src/native/NativeUsbBridge.cpp'):
            command[i] = str(source)
            changed_source = True
        elif i and command[i - 1] == '-o':
            command[i] = str(output)
            changed_output = True
    if not changed_source or not changed_output or '-c' not in command:
        raise RuntimeError('Target compile command did not contain the expected source/output')
    if c_compiler:
        compiler = Path(command[0])
        if not compiler.name.endswith('g++'):
            raise RuntimeError('Expected C++ target compiler for command derivation')
        command[0] = str(tool(compiler, 'gcc'))
        command = [arg for arg in command if not arg.startswith('-std=') and
                   arg not in ('-fno-rtti', '-Wno-bidi-chars')]
        command.append('-std=gnu11')
    command.extend(['-fPIC', '-fvisibility=hidden', '-I' + str(ROOT / 'sdk/driver')])
    command.extend(extra)
    subprocess.run(command, cwd=entry.get('directory', str(ROOT)), check=True)
    if not output.is_file() or not output.stat().st_size:
        raise RuntimeError('Missing target object: ' + str(output))


def idf_usb_source():
    checkout = OUTPUT / 'esp-idf-v4.4.7'
    if not (checkout / 'components/usb/usb_host.c').is_file():
        if checkout.exists():
            raise RuntimeError('Incomplete IDF source checkout: ' + str(checkout))
        subprocess.run(['git', 'clone', '--depth=1', '--branch', IDF_TAG,
                        '--filter=blob:none', '--sparse',
                        'https://github.com/espressif/esp-idf.git', str(checkout)],
                       cwd=ROOT, check=True)
        subprocess.run(['git', '-C', str(checkout), 'sparse-checkout', 'set',
                        'components/usb'], check=True)
    commit = subprocess.check_output(['git', '-C', str(checkout), 'rev-parse',
                                      'HEAD'], text=True).strip()
    (OUTPUT / 'idf-usb-source.txt').write_text(f'{IDF_TAG} {commit}\n')
    component = checkout / 'components/usb'
    if any(not (component / name).is_file() for name in USB_SOURCES):
        raise RuntimeError('ESP-IDF USB component sources differ from the pinned build')
    return component


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
        raise RuntimeError('Expected one ESP32-S3 USB compile command, got ' + str(len(matching)))
    entry = matching[0]
    argv = list(entry['arguments']) if 'arguments' in entry else shlex.split(entry['command'])
    controller = OUTPUT / 'controller.o'
    compile_target(argv, entry, SOURCE, controller, extra=('-Wall', '-Wextra', '-Werror'))
    compiler = Path(argv[0])
    nm = tool(compiler, 'nm')
    undefined = subprocess.check_output([str(nm), '-u', str(controller)], text=True)
    if 'usb_host_install' not in undefined or 'usb_host_transfer_submit' not in undefined:
        raise RuntimeError('Physical controller does not invoke the real IDF host implementation')
    if any(name in undefined for name in ('nativeUsb', 'NativeUsbBridge', 't5_usb_')):
        raise RuntimeError('Controller depends on compiled resident USB implementation')
    print('Physical controller compiled against ESP32-S3 IDF: PASS', flush=True)
    if not args.link_experiment:
        print('USB source linkage and OS/CPU imports not checked without --link-experiment', flush=True)
        return

    usb = idf_usb_source()
    includes = ('-I' + str(usb / 'include'), '-I' + str(usb / 'private_include'))
    objects = [controller]
    for name in USB_SOURCES:
        obj = OUTPUT / (name + '.o')
        compile_target(argv, entry, usb / name, obj, c_compiler=True, extra=includes)
        objects.append(obj)
    elf = OUTPUT / 'controller-link-experiment.elf'
    command = [str(compiler), '-shared', '-nostdlib', '-nostartfiles',
               '-Wl,--hash-style=sysv', '-Wl,--exclude-libs,ALL',
               '-Wl,-Bsymbolic', *map(str, objects), '-lgcc', '-o', str(elf)]
    subprocess.run(command, cwd=ROOT, check=True)
    imported = subprocess.check_output([str(nm), '-u', str(elf)], text=True)
    (OUTPUT / 'unresolved-symbols.txt').write_text(imported)
    bad_namespaces = ('usb_host_', 'usbh_', 'hcd_', 'hub_', 'usb_phy_',
                      'usb_new_phy', 'urb_')
    offenders = [line.strip() for line in imported.splitlines()
                 if any(namespace in line for namespace in bad_namespaces)]
    if offenders:
        print('Unresolved USB implementation symbols after PIC link:',
              *offenders, sep='\n  ', flush=True)
        raise RuntimeError('IDF USB implementation not fully linked into controller ELF')
    defined = subprocess.check_output([str(nm), '-D', '--defined-only', str(elf)], text=True)
    exports = [line.split()[-1] for line in defined.splitlines()]
    if exports != ['t5_driver_get']:
        raise RuntimeError('Controller ELF exports more than its driver entry point: ' + repr(exports))
    readelf = tool(compiler, 'readelf')
    dynamic = subprocess.check_output([str(readelf), '-d', str(elf)], text=True)
    if 'TEXTREL' in dynamic:
        raise RuntimeError('Controller has unsafe dynamic text/read-only relocations')
    print('Seven PIC-built IDF USB sources linked into controller ELF: PASS', flush=True)
    print('Generic OS/CPU import compatibility and on-device hardware still need verification.', flush=True)


if __name__ == '__main__':
    run()
