#!/usr/bin/env python3
"""Build a native C app with the SAME Xtensa S3 compiler used by this firmware."""
import argparse
import os
import pathlib
import shutil
import subprocess
from app_manifest import validate_manifest
from native_app_symbols import firmware_exports, validate_imports

repo = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('source', type=pathlib.Path)
parser.add_argument('--output', type=pathlib.Path, required=True)
parser.add_argument('--require-manifest', action='store_true')
parser.add_argument('--cc', default=os.environ.get('NATIVE_APP_CC'))
args = parser.parse_args()
manifest = None
if args.require_manifest or args.source.with_suffix('.json').exists():
    manifest = validate_manifest(args.source, args.output)
cc = args.cc or shutil.which('xtensa-esp32s3-elf-gcc')
if not cc:
    core = pathlib.Path(os.environ.get('PLATFORMIO_CORE_DIR', pathlib.Path.home() / '.platformio'))
    cc = str(core / 'packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc')
args.output.parent.mkdir(parents=True, exist_ok=True)

# Keep ELF linkage restricted. Pulling in the stock libgcc archive resolved
# __udivdi3 but produced an ELF rejected by esp_elf_validate_file(); it must
# not be used as a blanket linker dependency for native applications.
flags = [cc, '-std=c11', '-Os', '-fPIC', '-mtext-section-literals', '-mlongcalls',
         '-fvisibility=hidden', '-nostdlib', '-nostartfiles', '-shared',
         '-I' + str(repo / 'lib/NativeApps/include'),
         '-I' + str(repo / 'sdk/driver'), '-Wl,--hash-style=sysv']
readelf = cc.replace('gcc', 'readelf')

def build(support=()):
    subprocess.run([*flags, str(args.source), *map(str, support), '-o', str(args.output)], check=True)
    return subprocess.check_output([readelf, '--dyn-syms', '--wide', str(args.output)], text=True)

info = build()
# Xtensa lowers unsigned 64-bit division to a compiler helper. Supply the
# bounded, source-owned implementation only to an app that actually imports
# it. It performs no division itself and does not expand the firmware ABI.
if any(len(fields := line.split()) >= 8 and fields[4] == 'GLOBAL'
       and fields[6] == 'UND' and fields[7] == '__udivdi3'
       for line in info.splitlines()):
    helper = repo / 'lib/NativeApps/src/UnsignedDivisionCompat.c'
    info = build((helper,))

if not any('GLOBAL' in line and 'FUNC' in line and 'UND' not in line and line.split()[-1] == 'app_main'
           for line in info.splitlines() if line.strip()):
    raise SystemExit('The app must export void app_main(void) with default visibility')
validate_imports(info, firmware_exports(repo))
print(info)
if manifest:
    shutil.copyfile(manifest, args.output.with_suffix('.json'))
print('Copy', args.output, 'and its .json sidecar to /Apps/ on the SD card.')
