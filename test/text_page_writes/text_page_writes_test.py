#!/usr/bin/env python3
"""Production page/text/image serialization, Section callback and volume HAL writes.
Storage, scheduler, clock and font rendering endpoints are explicit host fixtures.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--text-source', type=Path)
parser.add_argument('--hal-source', type=Path)
parser.add_argument('--baseline', action='store_true')
parser.add_argument('--legacy', action='store_true')
parser.add_argument('--snapshot', type=Path, help='write full binary request, payload and render snapshots')
args = parser.parse_args()
here = Path(__file__).resolve().parent
root = here.parents[1]
hal = (args.hal_source or root / 'lib/hal/HalStorageVolume.cpp').read_text()
a = hal.index('size_t HalFile::write(const void *buffer, size_t count) {')
b = hal.index('size_t HalFile::write(uint8_t', a)
section = (root / 'lib/Epub/Epub/Section.cpp').read_text()
sa = section.index('uint32_t Section::onPageComplete(')
sb = section.index('\n}\n', sa) + 3
img = (root / 'lib/Epub/Epub/blocks/ImageBlock.cpp').read_text()
ia = img.index('bool ImageBlock::serialize(')
with tempfile.TemporaryDirectory(prefix='text-page-writes-') as temp:
    temp = Path(temp)
    (temp / 'hal.cpp').write_text('#include <HalStorage.h>\n' + hal[a:b])
    (temp / 'SectionWrite.inc').write_text(section[sa:sb])
    (temp / 'ImageSerialization.inc').write_text(img[ia:])
    flags = ['-std=c++17', '-O2', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-parameter']
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    if args.baseline or args.legacy:
        flags += ['-DTEST_BASELINE=1']
    if args.hal_source:
        flags += ['-DTEST_ORIGINAL_HAL=1']
    if not args.legacy:
        flags += ['-DBOARD_XTEINK_X4_PRO=1']
    incs = [here / 'stubs', temp, root / 'lib/hal', root / 'lib/Serialization', root / 'lib/Epub', root / 'lib/Epub/Epub', root / 'lib/Epub/Epub/blocks']
    source = args.text_source or root / 'lib/Epub/Epub/blocks/TextBlock.cpp'
    cmd = ['c++', *flags, *['-I' + str(p) for p in incs], str(root / 'lib/Epub/Epub/Page.cpp'),
           str(source.resolve()), str(temp / 'hal.cpp'), str(here / 'text_page_writes_test.cpp'), '-o', str(temp / 'test')]
    subprocess.run(cmd, check=True, timeout=90)
    env = os.environ.copy()
    if args.snapshot:
        args.snapshot.write_bytes(b'')
        env['WRITE_SNAPSHOT_PATH'] = str(args.snapshot.resolve())
    subprocess.run([str(temp / 'test')], check=True, timeout=90, env=env)
