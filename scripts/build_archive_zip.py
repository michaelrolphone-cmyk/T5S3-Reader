#!/usr/bin/env python3
"""Build the independently installable software archive.zip service; no publication."""
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
from generate_provider_package_inputs_v1 import canonical_manifest
from native_app_symbols import firmware_exports, privileged_os_cpu_exports, validate_imports
from probe_usb_controller_esp32s3 import compile_target
from normalize_xtensa_relocations import normalize
from verify_provider_relocation_map import audit_loader_map

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'Services/archive_zip'


def build(cxx=None):
    manifest = json.loads((SOURCE / 'manifest.json').read_text())
    if (manifest.get('type') != 'service' or manifest.get('id') != 'archive-zip' or
            canonical_manifest(SOURCE / 'manifest.json') != ('archive.zip', 1)):
        raise ValueError('invalid archive service manifest')
    subprocess.run(['pio', 'run', '-e', 't5s3-pro', '-t', 'compiledb'], cwd=ROOT, check=True)
    entries = json.loads((ROOT / 'compile_commands.json').read_text())
    matches = [entry for entry in entries if Path(entry['file']).as_posix().endswith('src/native/NativeUsbBridge.cpp')]
    if len(matches) != 1:
        raise ValueError('missing unique target compile configuration')
    entry = matches[0]
    args = list(entry['arguments']) if 'arguments' in entry else shlex.split(entry['command'])
    cxx = args[0]
    output = ROOT / 'dist/experimental' / manifest['id']
    output.mkdir(parents=True, exist_ok=True)
    elf = output / manifest['file_name']
    obj = output / 'archive.o'
    compile_target(args, entry, SOURCE / 'service.cpp', obj,
                   extra=('-Wall', '-Wextra', '-Werror', '-mtext-section-literals', '-mlongcalls', '-fno-exceptions', '-fno-rtti',
                          '-fno-unwind-tables', '-fno-asynchronous-unwind-tables'))
    subprocess.run([cxx, '-shared', '-nostdlib', '-nostartfiles', '-Wl,--hash-style=sysv',
                    '-Wl,--exclude-libs,ALL', str(obj), '-lgcc', '-o', str(elf)], cwd=ROOT, check=True)
    normalize(elf)
    readelf = str(Path(cxx).with_name(Path(cxx).name.replace('g++', 'readelf')))
    symbols = subprocess.check_output([readelf, '--dyn-syms', '--wide', str(elf)], text=True)
    exported = {fields[7] for line in symbols.splitlines()
                if len(fields := line.split()) >= 8 and fields[4] == 'GLOBAL'
                and fields[6] != 'UND' and fields[3] == 'FUNC'}
    if exported != {'t5_driver_get'}:
        raise ValueError('archive service must export only its provider descriptor')
    imported = validate_imports(symbols, {name for name in firmware_exports(ROOT) if not name.startswith('t5_')} | privileged_os_cpu_exports(ROOT))
    # Exact target flags retain stack protection. These two ABI-v1 imports
    # are already admitted OS/CPU primitives; do not disable stack checking.
    if not imported <= {'memcpy', 'memset', 'strcmp', 'strlen', 'memcmp', 'strncmp', '__assert_func',
                        'xQueueCreateMutex', 'xQueueGenericCreate', 'xQueueGenericSend',
                        'xQueueSemaphoreTake', 'uxQueueMessagesWaiting', 'vQueueDelete',
                        'vTaskDelay', 'xTaskGetTickCount', '__stack_chk_guard', '__stack_chk_fail'}:
        raise ValueError('archive service has unexpected imports: ' + repr(imported))
    mapping = audit_loader_map(elf)
    if (mapping['unmapped_relocations'] or mapping['unmapped_relative_values'] or
            mapping['unmapped_executable_sections'] or mapping['absolute_peripheral_relocations']):
        raise ValueError('archive service must have a complete software-only relocation map')
    payload = elf.read_bytes()
    if not 52 <= len(payload) <= 1024 * 1024:
        raise ValueError('archive ELF exceeds its ordinary entry bound')
    manifest.update(size_bytes=len(payload), sha256=hashlib.sha256(payload).hexdigest())
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return elf


if __name__ == '__main__':
    print(build())
