#!/usr/bin/env python3
"""Run production BookMetadataCache/Expat navigation with counted storage I/O."""
from pathlib import Path
import os
import subprocess
import tempfile
HERE = Path(__file__).resolve().parent
ROOT = Path(os.environ.get('EPUB_SOURCE_ROOT', str(HERE.parents[1])))
flags = ['-std=c++17', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
         '-Wno-unused-function', '-Wno-unused-variable', '-Wno-misleading-indentation',
         '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections']
if 'bool resetTocEntries();' in (ROOT / 'lib/Epub/Epub/BookMetadataCache.h').read_text():
    flags += ['-DPERF_HAS_TOC_RESET']
if os.environ.get('EPUB_BASELINE') == '1':
    flags += ['-DPERF_EXPECT_BASELINE']
if os.environ.get('EPUB_SANITIZE') == '1':
    flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
incs = [HERE / 'stubs', ROOT / 'lib/Serialization', ROOT / 'lib/ZipFile',
        ROOT / 'lib/Epub/Epub', ROOT / 'lib/Epub/Epub/parsers', ROOT / 'lib/XmlParserUtils']
with tempfile.TemporaryDirectory(prefix='epub-toc-index-') as tmp:
    output = str(Path(tmp) / 'test')
    subprocess.run([os.environ.get('CXX', 'c++'), *flags, *('-I' + str(p) for p in incs),
                    str(ROOT / 'lib/Epub/Epub/BookMetadataCache.cpp'),
                    str(ROOT / 'lib/Epub/Epub/parsers/TocNavParser.cpp'),
                    str(HERE / 'index_test.cpp'), '-lexpat', '-o', output], check=True, timeout=90)
    subprocess.run([output], check=True, timeout=90)
