#!/usr/bin/env python3
"""Build a real generic clock ELF from existing OS libc primitives, not a firmware proxy."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from native_app_symbols import firmware_exports, validate_imports
from normalize_xtensa_relocations import normalize

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'Drivers/platform_clock_v1'
OUTPUT = ROOT / 'dist/experimental/platform-clock-v1'


def build(cc=None):
    manifest = json.loads((SOURCE / 'manifest.json').read_text())
    required = {
        'type': 'driver', 'id': 'platform-clock-v1', 'driver_abi': 2,
        'architecture': 'xtensa-esp32s3', 'file_name': 'driver.elf',
        'requires': [], 'provides': [{'capability': 'platform.clock', 'api': 1}],
        'status': 'experimental-unpublished',
    }
    if any(manifest.get(k) != v for k, v in required.items()):
        raise ValueError('Invalid platform-clock driver manifest')
    cc = cc or os.environ.get('NATIVE_DRIVER_CC') or shutil.which('xtensa-esp32s3-elf-gcc')
    if not cc:
        core = Path(os.environ.get('PLATFORMIO_CORE_DIR', Path.home() / '.platformio'))
        cc = str(core / 'packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc')
    OUTPUT.mkdir(parents=True, exist_ok=True)
    elf = OUTPUT / 'driver.elf'
    # Xtensa binutils 2.35 can crash in elf_xtensa_finish_dynamic_sections
    # when garbage-collecting a small PIC shared object. Keep complete tiny
    # clock sections and use the same proven flags as the other small ELFs.
    subprocess.run([
        cc, '-std=c11', '-D_DEFAULT_SOURCE', '-Os', '-fPIC',
        '-mtext-section-literals', '-mlongcalls', '-fvisibility=hidden',
        '-nostdlib', '-nostartfiles', '-shared',
        '-I' + str(ROOT / 'sdk/driver'),
        '-Wl,--hash-style=sysv', '-Wl,--exclude-libs,ALL',
        str(SOURCE / 'driver.c'), '-lgcc', '-o', str(elf),
    ], check=True)
    # This specific Xtensa linker sometimes appends two literal all-zero
    # R_XTENSA_NONE entries after the real dynamic relocations. The native
    # validator rejects them correctly. Trim ONLY those unused metadata
    # slots, updating .rela.dyn and DT_RELASZ consistently without touching
    # any real relocation, address or code. All ELF validator checks remain.
    removed = normalize(elf)
    print('Clock link: excluded trailing zero relocation slots:', removed)
    readelf = str(Path(cc).with_name(Path(cc).name.replace('gcc', 'readelf')))
    symbols = subprocess.check_output([readelf, '--dyn-syms', '--wide', str(elf)], text=True)
    exported = {fields[7] for line in symbols.splitlines()
                if len(fields := line.split()) >= 8 and fields[4] == 'GLOBAL'
                and fields[6] != 'UND' and fields[3] == 'FUNC'}
    if exported != {'t5_driver_get'}:
        raise ValueError('Platform clock must export only the generic driver root: ' + repr(exported))
    imported = validate_imports(symbols, {s for s in firmware_exports(ROOT)
                                           if not s.startswith('t5_')})
    if imported != {'clock_gettime', 'usleep'}:
        raise ValueError('Clock ELF imported unexpected native functions: ' + repr(imported))
    payload = elf.read_bytes()
    if not 52 <= len(payload) <= 256 * 1024 or payload[:7] != b'\x7fELF\x01\x01\x01' or \
            int.from_bytes(payload[16:18], 'little') != 3 or \
            int.from_bytes(payload[18:20], 'little') != 94:
        raise ValueError('Invalid Xtensa generic clock ELF')
    manifest.update(size_bytes=len(payload), sha256=hashlib.sha256(payload).hexdigest())
    (OUTPUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Platform clock independent ELF: built, exact loader libc imports verified; '
          'not installable until provider graph and executor cutover')
    return elf


if __name__ == '__main__':
    build()
