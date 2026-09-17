#!/usr/bin/env python3
"""Compile real usb.controller provider source with the firmware's ESP32-S3 SDK.

This is a REAL-IDF compile and a non-publishing relocation/link probe, not a
mock or a claim that the firmware ELF loader can yet load the resulting object.
The ESP-IDF host archive must be linked into the provider, rather than exporting
USB functions from RiscRTE. Until the separate VBUS ELF and generic OS imports
are available, do not place this controller in a package/catalog.
"""
import argparse
import json
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'Drivers/usb_controller_esp32s3/driver.cpp'
OUTPUT = ROOT / 'dist/experimental/usb-controller-esp32s3'


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
    object_file = OUTPUT / 'controller.o'
    replaced_source = replaced_output = False
    for i, arg in enumerate(argv):
        if Path(arg).as_posix().endswith('src/native/NativeUsbBridge.cpp'):
            argv[i] = str(SOURCE)
            replaced_source = True
        elif i and argv[i - 1] == '-o':
            argv[i] = str(object_file)
            replaced_output = True
    if not replaced_source or not replaced_output or '-c' not in argv:
        raise RuntimeError('Cannot safely reuse the real target compilation configuration')
    argv.extend(['-fPIC', '-fvisibility=hidden', '-Wall', '-Wextra', '-Werror',
                 '-I' + str(ROOT / 'sdk/driver')])
    subprocess.run(argv, cwd=entry.get('directory', str(ROOT)), check=True)
    if not object_file.is_file() or not object_file.stat().st_size:
        raise RuntimeError('Physical controller source did not produce an Xtensa object')
    compiler = Path(argv[0])
    nm = compiler.with_name(compiler.name.replace('g++', 'nm').replace('gcc', 'nm'))
    undefined = subprocess.check_output([str(nm), '-u', str(object_file)], text=True)
    if 'usb_host_install' not in undefined or 'usb_host_transfer_submit' not in undefined:
        raise RuntimeError('Physical controller does not reference actual IDF host implementation')
    if any(forbidden in undefined for forbidden in ('nativeUsb', 'NativeUsbBridge', 't5_usb_')):
        raise RuntimeError('USB hardware provider depends on resident firmware USB implementation')
    print('Physical ESP32-S3 controller source compiled with real IDF headers: PASS')
    print('IDF USB implementation must be linked into ELF; unresolved OS/USB symbols are NOT installable.')
    if args.link_experiment:
        core = Path.home() / '.platformio'
        # Follow the exact toolchain chosen by PlatformIO; do not guess an SDK version.
        framework = core / 'packages/framework-arduinoespressif32/tools/sdk/esp32s3/lib/libusb.a'
        if not framework.is_file():
            raise FileNotFoundError('SDK USB archive not found: ' + str(framework))
        experiment = OUTPUT / 'controller-link-experiment.elf'
        command = [str(compiler), '-shared', '-nostdlib', '-nostartfiles',
                   '-Wl,--hash-style=sysv', str(object_file),
                   '-Wl,--start-group', str(framework), '-lgcc', '-Wl,--end-group',
                   '-o', str(experiment)]
        subprocess.run(command, cwd=ROOT, check=True)
        imported = subprocess.check_output([str(nm), '-u', str(experiment)], text=True)
        (OUTPUT / 'unresolved-symbols.txt').write_text(imported)
        if 'usb_host_' in imported:
            raise RuntimeError('IDF USB archive was not fully incorporated into provider ELF')
        print('IDF USB archive link probe completed; verify generic OS/CPU imports before installing')


if __name__ == '__main__':
    run()
