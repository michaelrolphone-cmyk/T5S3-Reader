#!/usr/bin/env python3
"""Production TXT paging/Markdown and real Noto Sans metrics; fixture storage/UI.

The transcript includes every measurement argument/style, every output line,
offset/fence result and storage/cleanup/scheduling event. Baseline and repaired
code must have the same transcript; only boundary-work counters may differ.
"""
import argparse
import hashlib
import os
import re
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--baseline-ref')
parser.add_argument('--enforce-cost', action='store_true')
parser.add_argument('--output', type=Path)
parser.add_argument('--compare', type=Path)
args = parser.parse_args()

def source(path):
    if args.baseline_ref:
        return subprocess.check_output(['git', '-C', str(ROOT), 'show', args.baseline_ref + ':' + path], text=True)
    return (ROOT / path).read_text()

def function(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    depth = 0
    for i in range(opening, len(text)):
        depth += (text[i] == '{') - (text[i] == '}')
        if depth == 0:
            return text[start:i + 1]
    raise AssertionError('unterminated production function ' + signature)

with tempfile.TemporaryDirectory(prefix='txt-paging-') as temp:
    d = Path(temp)
    (d / 'GfxRenderer.h').write_text((ROOT / 'test/txt_paging/fixture.h').read_text())
    actual = source('lib/GfxRenderer/GfxRenderer.cpp')
    methods = '\n'.join(function(actual, sig) for sig in (
        'uint8_t resolveSdCardStyle(', 'uint16_t sdCardAdvance(',
        'int GfxRenderer::getTextAdvanceX(', 'int GfxRenderer::getLineHeight('))
    start = methods.index('int GfxRenderer::getTextAdvanceX(')
    opening = methods.index('{', start) + 1
    methods = methods[:opening] + '''
    ++work.measures; work.measuredBytes += std::strlen(text);
    if (style == EpdFontFamily::BOLD) ++work.boldMeasures;
    std::cout << "measure " << fontId << ' ' << unsigned(style) << '\\n';
    recordText(text);
''' + methods[opening:]
    (d / 'measurement.cpp').write_text('#include <GfxRenderer.h>\n#include <Utf8.h>\n#include <Logging.h>\n' + methods)
    paging = source('src/activities/reader/TxtReaderPaging.cpp')
    boundary = function(paging, 'size_t nextUtf8Boundary(')
    boundary = boundary.replace('  if (pos >= text.size())', '  ++work.boundaries; const size_t before = pos;\n  if (pos >= text.size())')
    boundary = boundary.replace('  return pos;', '  work.boundaryBytes += pos - before;\n  return pos;')
    page = function(paging, 'bool TxtReaderActivity::loadPageAtOffset(')
    page = page.replace('utf8Boundaries.reserve(', '++work.boundaryTables;\n    utf8Boundaries.reserve(')
    page = page.replace('utf8Boundaries.push_back(',
                        'if (utf8Boundaries.size() == utf8Boundaries.capacity()) ++work.boundaryGrowths;\n      utf8Boundaries.push_back(')
    page = re.sub(r'(utf8Boundaries.push_back\([^;]+;)',
                  r'\1\n      work.maxBoundaries = std::max(work.maxBoundaries, utf8Boundaries.size());', page)
    body = '\n'.join([
        'constexpr size_t CHUNK_SIZE = 8 * 1024;',
        function(paging, 'bool isUtf8ContinuationByte('), boundary,
        '#define malloc testMalloc\n#define free testFree\n#define vTaskDelay testDelay',
        '#define LOG_ERR(...) ((void)0)',
        page,
        '#undef malloc\n#undef free\n#undef vTaskDelay\n#undef LOG_ERR'])
    (d / 'paging.inc').write_text(body)
    # All font, UTF-8 and Markdown implementations are unchanged production.
    files = [ROOT / 'test/txt_paging/fixture.cpp', d / 'measurement.cpp']
    for path in ['lib/EpdFont/EpdFont.cpp', 'lib/EpdFont/EpdFontFamily.cpp',
                 'lib/EpdFont/SdCardFont.cpp', 'lib/Utf8/Utf8.cpp', 'lib/Markdown/Markdown.cpp']:
        target = d / Path(path).name
        target.write_text(source(path))
        files.append(target)
    cmd = [os.environ.get('CXX', 'c++'), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror']
    if os.environ.get('TXT_SANITIZE') == '1':
        cmd += ['-fsanitize=address,undefined', '-fno-sanitize-recover=undefined', '-fno-omit-frame-pointer']
    if args.enforce_cost:
        cmd += ['-DENFORCE_COST']
    cmd += ['-I' + str(p) for p in [d, ROOT / 'test/txt_paging', ROOT / 'test/fontawesome/stubs',
                                    ROOT / 'lib/EpdFont', ROOT / 'lib/Utf8', ROOT / 'lib/Markdown']]
    subprocess.run(cmd + [str(p) for p in files] + ['-o', str(d / 'test')], check=True)
    result = subprocess.run([str(d / 'test')], capture_output=True, timeout=240)
    print(result.stderr.decode(), end='')
    result.check_returncode()
    if args.output:
        args.output.write_bytes(result.stdout)
    if args.compare:
        assert result.stdout == args.compare.read_bytes(), 'TXT paging measurement/output/effect transcript changed'
    digest = hashlib.sha256(result.stdout).hexdigest()
    # Full unmodified b91bc323 transcript: every metric argument/style, line,
    # offset/fence result and modeled storage/cleanup/scheduling effect.
    expected = 'cdef11cc580573cd1f0dfb78ced57d520d790321317228f3f8813cd65b5c44f7'
    assert digest == expected, 'Original TXT paging transcript changed: ' + digest
    print('TXT paging transcript SHA256=' + digest)
