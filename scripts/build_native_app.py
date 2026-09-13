#!/usr/bin/env python3
"""Build a native C app with the SAME Xtensa S3 compiler used by this firmware."""
import argparse
import os
import pathlib
import shutil
import subprocess

repo = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('source', type=pathlib.Path)
parser.add_argument('--output', type=pathlib.Path, required=True)
parser.add_argument('--cc', default=os.environ.get('NATIVE_APP_CC'))
args = parser.parse_args()
cc = args.cc or shutil.which('xtensa-esp32s3-elf-gcc')
if not cc:
    core = pathlib.Path(os.environ.get('PLATFORMIO_CORE_DIR', pathlib.Path.home() / '.platformio'))
    cc = str(core / 'packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc')
args.output.parent.mkdir(parents=True, exist_ok=True)
subprocess.run([cc, '-std=c11', '-Os', '-fPIC', '-mtext-section-literals', '-mlongcalls',
                '-fvisibility=hidden', '-nostdlib', '-nostartfiles', '-shared',
                '-I' + str(repo / 'lib/NativeApps/include'),
                '-Wl,--hash-style=sysv', str(args.source), '-o', str(args.output)], check=True)
readelf = cc.replace('gcc', 'readelf')
info = subprocess.check_output([readelf, '--dyn-syms', '--wide', str(args.output)], text=True)
if not any('GLOBAL' in line and 'FUNC' in line and 'UND' not in line and line.split()[-1] == 'app_main'
           for line in info.splitlines() if line.strip()):
    raise SystemExit('The app must export void app_main(void) with default visibility')
print(info)
print('Copy', args.output, 'to /apps/ on the SD card and open it in Browse Files.')
