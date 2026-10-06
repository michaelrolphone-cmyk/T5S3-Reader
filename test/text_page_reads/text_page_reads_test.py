#!/usr/bin/env python3
"""Real Page/TextBlock/serialization/section-loader/HAL; explicit storage/font fixtures."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--text-source', type=Path)
parser.add_argument('--baseline', action='store_true')
parser.add_argument('--legacy', action='store_true', help='verify unchanged non-volume TextBlock caller')
args = parser.parse_args()
here = Path(__file__).resolve().parent
root = here.parents[1]
hal = (root / 'lib/hal/HalStorageVolume.cpp').read_text()
start = hal.index('int HalFile::read(void *buffer, size_t count) {')
end = hal.index('\nint HalFile::read()', start)
section = (root / 'lib/Epub/Epub/Section.cpp').read_text()
a = section.index('std::unique_ptr<Page> Section::loadPageFromSectionFile()')
b = section.index('\n}\n', a) + 3
img = (root / 'lib/Epub/Epub/blocks/ImageBlock.cpp').read_text()
ia = img.index('bool ImageBlock::serialize(')
with tempfile.TemporaryDirectory(prefix='text-page-reads-') as temp:
    temp = Path(temp)
    (temp / 'hal.cpp').write_text('#include <HalStorage.h>\n' + hal[start:end])
    (temp / 'SectionRead.inc').write_text(section[a:b])
    (temp / 'ImageSerialization.inc').write_text(img[ia:])
    flags = ['-std=c++17', '-O2', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-parameter']
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    if args.baseline or args.legacy:
        flags += ['-DTEST_BASELINE=1']
    if not args.legacy:
        flags += ['-DBOARD_XTEINK_X4_PRO=1']
    includes = [here / 'stubs', temp, root / 'lib/hal', root / 'lib/Serialization',
                root / 'lib/Epub', root / 'lib/Epub/Epub', root / 'lib/Epub/Epub/blocks']
    source = args.text_source or root / 'lib/Epub/Epub/blocks/TextBlock.cpp'
    cmd = ['c++', *flags, *['-I' + str(p) for p in includes], str(root / 'lib/Epub/Epub/Page.cpp'),
           str(source.resolve()), str(temp / 'hal.cpp'), str(here / 'text_page_reads_test.cpp'), '-o', str(temp / 'test')]
    subprocess.run(cmd, check=True, timeout=90)
    subprocess.run([str(temp / 'test')], check=True, timeout=90, env=os.environ.copy())
