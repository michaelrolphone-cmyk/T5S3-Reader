#!/usr/bin/env python3
"""Complete production font path with shipped CJK data and counted membership.

Instrumentation changes only comparison counting in a temporary source copy.
CODEPOINT_SOURCE_ROOT and CODEPOINT_EXPECT_BASELINE preserve the original-fails
cost control. CODEPOINT_SNAPSHOT writes exact admitted sets/advances/error states.
"""
from pathlib import Path
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = Path(os.environ.get('CODEPOINT_SOURCE_ROOT', HERE.parents[1]))
source = Path(os.environ.get('CODEPOINT_SOURCE_FILE', ROOT / 'lib/EpdFont/SdCardFont.cpp')).read_text()
start = source.index('bool collectUniqueCodepoints(')
end = source.index('\nconst char* asCStr', start)
helper = source[start:end]
indexed = 'std::lower_bound(codepoints, end, cp)' in helper
if indexed:
    helper = helper.replace('std::lower_bound(codepoints, end, cp)',
        'std::lower_bound(codepoints, end, cp, [](uint32_t a, uint32_t b) { ++membershipComparisons; return a < b; })')
    helper = helper.replace('*found != cp', '(++membershipComparisons, *found != cp)')
    helper = helper.replace('std::move_backward(found, end, end + 1);',
        'shiftedWords += end - found; std::move_backward(found, end, end + 1);')
else:
    assert helper.count('if (codepoints[i] == cp)') == 1
    helper = helper.replace('if (codepoints[i] == cp)', 'if ((++membershipComparisons, codepoints[i] == cp))')
source = source[:start] + helper + source[end:]
source = source.replace('namespace {', 'uint64_t membershipComparisons = 0, shiftedWords = 0;\nnamespace {', 1)
flags = ['-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror']
if indexed:
    flags += ['-DINDEXED_COLLECTION']
if os.environ.get('CODEPOINT_EXPECT_BASELINE') == '1':
    flags += ['-DEXPECT_BASELINE']
if os.environ.get('CODEPOINT_SANITIZE') == '1':
    flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all']
with tempfile.TemporaryDirectory(prefix='font-codepoints-') as directory:
    temp = Path(directory)
    (temp / 'production.cpp').write_text(source)
    (temp / 'test.cpp').write_text('#include "production.cpp"\n' + (HERE / 'codepoint_dedup_test.cpp').read_text())
    command = ['c++', *flags]
    command += ['-I' + str(p) for p in (HERE / 'dedup_stubs', ROOT / 'test/fontawesome/stubs', ROOT / 'lib/EpdFont', ROOT / 'lib/Utf8')]
    command += [str(temp / 'test.cpp')]
    command += [str(ROOT / p) for p in ('lib/EpdFont/EpdFont.cpp', 'lib/EpdFont/EpdFontFamily.cpp', 'lib/Utf8/Utf8.cpp')]
    command += ['-o', str(temp / 'test')]
    subprocess.run(command, check=True, timeout=90)
    subprocess.run([str(temp / 'test'), str(ROOT)], check=True, timeout=90)
