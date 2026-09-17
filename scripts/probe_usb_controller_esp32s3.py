#!/usr/bin/env python3
"""Compile real usb.controller provider against the ESP32-S3 SDK and link IDF USB.

This uses the exact target compile command and embeds the IDF USB static archive
inside the provider ELF. It never exports USB APIs from the resident firmware.
The output remains non-installable until the external import/relocation audit,
board VBUS provider, generic runtime integration and physical tests are done.
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
    print('Physical ESP32-S3 controller source compiled with real IDF headers: PASS', flush=True)
    if args.link_experiment:
        core = Path.home() / '.platformio'
        framework = core / 'packages/framework-arduinoespressif32/tools/sdk/esp32s3/lib/libusb.a'
        if not framework.is_file():
            raise FileNotFoundError('SDK USB archive not found: ' + str(framework))
        experiment = OUTPUT / 'controller-link-experiment.elf'
        # This archive was built for static firmware and contains direct Xtensa
        # calls to its own global definitions. Hide those definitions and bind
        # internal references locally; otherwise -shared produces dangerous
        # dynamic relocations into text/rodata. Do NOT export resident USB ABI.
        command = [str(compiler), '-shared', '-nostdlib', '-nostartfiles',
                   '-Wl,--hash-style=sysv', '-Wl,--exclude-libs,ALL',
                   '-Wl,-Bsymbolic', str(object_file),
                   '-Wl,--start-group', str(framework), '-lgcc', '-Wl,--end-group',
                   '-o', str(experiment)]
        subprocess.run(command, cwd=ROOT, check=True)
        imported = subprocess.check_output([str(nm), '-u', str(experiment)], text=True)
        (OUTPUT / 'unresolved-symbols.txt').write_text(imported)
        unresolved_usb = ('usb_host_', 'usbh_', 'hcd_', 'hub_', 'usb_phy_', 'usb_new_phy')
        if any(name in imported for name in unresolved_usb):
            raise RuntimeError('IDF USB archive not fully incorporated into provider ELF')
        defined = subprocess.check_output([str(nm), '-D', '--defined-only',
                                           str(experiment)], text=True)
        if [line.split()[-1] for line in defined.splitlines()] != ['t5_driver_get']:
            raise RuntimeError('Controller ELF exports more than its provider entry point: ' + defined)
        print('IDF USB implementation linked inside provider ELF: PASS', flush=True)
        print('Remaining generic OS/CPU imports require a separate loader/ABI audit.', flush=True)
    else:
        print('IDF USB archive linkage and generic OS imports have not been validated.', flush=True)


if __name__ == '__main__':
    run()
