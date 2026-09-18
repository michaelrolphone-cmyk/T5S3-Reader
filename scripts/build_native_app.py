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
# -nostdlib omits compiler runtime helpers as well as libc. Native apps can
# legitimately use C operations such as 64-bit integer division, which GCC
# lowers to __udivdi3 on Xtensa. Resolve those helpers from this toolchain's
# libgcc INTO the ELF, rather than expanding the firmware's public ABI or
# allowing an unresolved import. Keep archive symbols private to this app.
subprocess.run([cc, '-std=c11', '-Os', '-fPIC', '-mtext-section-literals', '-mlongcalls',
                '-fvisibility=hidden', '-nostdlib', '-nostartfiles', '-shared',
                '-I' + str(repo / 'lib/NativeApps/include'),
                '-Wl,--hash-style=sysv', '-Wl,--exclude-libs,ALL',
                str(args.source), '-lgcc', '-o', str(args.output)], check=True)
readelf = cc.replace('gcc', 'readelf')
info = subprocess.check_output([readelf, '--dyn-syms', '--wide', str(args.output)], text=True)
if not any('GLOBAL' in line and 'FUNC' in line and 'UND' not in line and line.split()[-1] == 'app_main'
           for line in info.splitlines() if line.strip()):
    raise SystemExit('The app must export void app_main(void) with default visibility')
validate_imports(info, firmware_exports(repo))
print(info)
if manifest:
    shutil.copyfile(manifest, args.output.with_suffix('.json'))
print('Copy', args.output, 'and its .json sidecar to /Apps/ on the SD card.')
