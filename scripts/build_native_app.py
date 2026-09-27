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
# compiler helpers but produced an ELF rejected by esp_elf_validate_file(); it
# must not be used as a blanket linker dependency for native applications.
flags = [cc, '-std=c11', '-Os', '-fPIC', '-mtext-section-literals', '-mlongcalls',
         '-fvisibility=hidden', '-nostdlib', '-nostartfiles', '-shared',
         '-I' + str(repo / 'lib/NativeApps/include'),
         '-I' + str(repo / 'sdk/driver'), '-Wl,--hash-style=sysv']
readelf = cc.replace('gcc', 'readelf')
strip = cc.replace('gcc', 'strip')


def build(support=()):
    subprocess.run([*flags, str(args.source), *map(str, support), '-o', str(args.output)], check=True)
    return subprocess.check_output([readelf, '--dyn-syms', '--wide', str(args.output)], text=True)


def undefined_globals(info):
    symbols = set()
    for line in info.splitlines():
        fields = line.split()
        if len(fields) >= 8 and fields[4] == 'GLOBAL' and fields[6] == 'UND':
            symbols.add(fields[7])
    return symbols


def validate_dynamic_abi(info):
    if not any('GLOBAL' in line and 'FUNC' in line and 'UND' not in line and line.split()[-1] == 'app_main'
               for line in info.splitlines() if line.strip()):
        raise SystemExit('The app must export void app_main(void) with default visibility')
    validate_imports(info, firmware_exports(repo))


info = build()
# Xtensa lowers operations that are not implemented directly by the core to
# compiler helpers. Supply bounded source-owned implementations only to apps
# that actually import them. These helpers do not expand the firmware ABI and
# avoid the unsupported ELF constructs pulled in by the stock libgcc archive.
support_by_symbol = {
    '__udivdi3': repo / 'lib/NativeApps/src/UnsignedDivisionCompat.c',
    '__divsf3': repo / 'lib/NativeApps/src/SingleFloatDivisionCompat.c',
}
undefined = undefined_globals(info)
support = tuple(path for symbol, path in support_by_symbol.items() if symbol in undefined)
if support:
    info = build(support)

validate_dynamic_abi(info)

# Release ELFs need only their dynamic symbols and relocation targets. Keeping
# the compiler's full local symbol/string tables wastes SD, catalog, download,
# and loader-budget bytes without helping the runtime. Strip only symbols that
# the linker marks unneeded, then revalidate the exact artifact we publish.
subprocess.run([strip, '--strip-unneeded', str(args.output)], check=True)
info = subprocess.check_output([readelf, '--dyn-syms', '--wide', str(args.output)], text=True)
validate_dynamic_abi(info)
print(info)
if manifest:
    shutil.copyfile(manifest, args.output.with_suffix('.json'))
print('Copy', args.output, 'and its .json sidecar to /Apps/ on the SD card.')
