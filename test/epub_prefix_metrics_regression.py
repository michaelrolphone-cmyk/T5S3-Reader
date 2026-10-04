#!/usr/bin/env python3
"""Complete ParsedText/Hyphenator + real fonts, verbatim renderer methods; fixture display/storage edges."""
from pathlib import Path
import argparse
import hashlib
import sys
import os
import subprocess
import tempfile

R = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--baseline-ref')
parser.add_argument('--compile-only', action='store_true', help='Cross-compile fixture and real translation units without linking')
parser.add_argument('--enforce-cost', action='store_true')
parser.add_argument('--output', type=Path)
parser.add_argument('--compare', type=Path)
args = parser.parse_args()

def source(path):
    if args.baseline_ref:
        return subprocess.check_output(['git', '-C', str(R), 'show', args.baseline_ref + ':' + path], text=True)
    return (R / path).read_text()

with tempfile.TemporaryDirectory(prefix='epub-prefix-') as temp:
    d = Path(temp)
    header = '''#pragma once
#include <EpdFontFamily.h>
#include <SdCardFont.h>
#include <map>
#include <string>
#include <vector>
class GfxRenderer {
public:
std::map<int,EpdFontFamily> fontMap;
std::map<int,SdCardFont*> sdCardFonts_;
bool isSdCardFont(int id)const{return sdCardFonts_.count(id)!=0;}
void ensureSdCardFontReady(int,const std::vector<std::string>&,bool,uint8_t)const;
int getTextAdvanceX(int,const char*,EpdFontFamily::Style=EpdFontFamily::REGULAR)const;
int getSpaceWidth(int,EpdFontFamily::Style=EpdFontFamily::REGULAR)const;
int getSpaceAdvance(int,uint32_t,uint32_t,EpdFontFamily::Style=EpdFontFamily::REGULAR)const;
int getKerning(int,uint32_t,uint32_t,EpdFontFamily::Style=EpdFontFamily::REGULAR)const;
'''
    if not args.baseline_ref:
        actual = source('lib/GfxRenderer/GfxRenderer.h')
        header += actual[actual.index('  struct TextPrefixMetrics'):actual.index('  int getTextAdvanceX(')]
    header += '};\nextern uint64_t measure_calls,measure_codepoints,prefix_calls;\n'
    (d / 'GfxRenderer.h').write_text(header)
    (d / 'Logging.h').write_text('#pragma once\ntemplate<class...T> void testLog(const char*, const char*, T...) {}\n#define LOG_ERR(...) testLog(__VA_ARGS__)\n#define LOG_DBG(...) testLog(__VA_ARGS__)\n')
    actual = source('lib/GfxRenderer/GfxRenderer.cpp')
    def function(signature):
        start = actual.index(signature)
        return actual[start:actual.index('\n}', start) + 2]
    methods = actual[actual.index('int GfxRenderer::getSpaceWidth('):actual.index('int GfxRenderer::getFontAscenderSize(')]
    # Counter-only instrumentation: both prefix and ordinary UTF-8 work is counted.
    pos = methods.index('int GfxRenderer::getTextAdvanceX(')
    brace = methods.index('{', pos) + 1
    methods = methods[:brace] + '\n  ++measure_calls;' + methods[brace:]
    if not args.baseline_ref:
        pos = methods.index('bool GfxRenderer::getTextPrefixMetrics(')
        brace = methods.index('{', pos) + 1
        methods = methods[:brace] + '\n  ++prefix_calls;' + methods[brace:]
    prefix = '#include <GfxRenderer.h>\n#include <Utf8.h>\n#include <Logging.h>\n'
    prefix += 'uint64_t measure_calls=0,measure_codepoints=0,prefix_calls=0;\n'
    prefix += 'uint32_t measuredNext(const uint8_t** p){auto cp=utf8NextCodepoint(p);if(cp)++measure_codepoints;return cp;}\n'
    prefix += function('uint8_t resolveSdCardStyle(') + '\n'
    prefix += function('void GfxRenderer::ensureSdCardFontReady(int fontId, const std::vector') + '\n'
    (d / 'measurement.cpp').write_text(prefix + '#define utf8NextCodepoint measuredNext\n' + methods + '\n#undef utf8NextCodepoint\n')
    (d / 'ParsedText.cpp').write_text(source('lib/Epub/Epub/ParsedText.cpp'))
    incs = [d, R/'test/fontawesome/stubs', R/'lib/Epub', R/'lib/Epub/Epub', R/'lib/EpdFont', R/'lib/Utf8']
    files = [R/'test/epub_prefix/metrics_test.cpp', d/'measurement.cpp', d/'ParsedText.cpp']
    files += [R / ('lib/EpdFont/' + x + '.cpp') for x in ['EpdFont', 'EpdFontFamily', 'SdCardFont']]
    files += [R/'lib/Utf8/Utf8.cpp']
    files += [R/('lib/Epub/Epub/hyphenation/'+x+'.cpp') for x in ['Hyphenator','LanguageRegistry','LiangHyphenation','HyphenationCommon']]
    cmd = [os.environ.get('CXX','c++'), '-std=c++17','-O1','-Wall','-Wextra','-Werror']
    if os.environ.get('PREFIX_SANITIZE') == '1': cmd += ['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    if args.baseline_ref: cmd += ['-DBASELINE']
    if args.enforce_cost: cmd += ['-DENFORCE_COST']
    cmd += ['-I'+str(p) for p in incs]
    if args.compile_only:
        for i, file in enumerate(files):
            subprocess.run(cmd + ['-c', str(file), '-o', str(d / f'unit-{i}.o')], check=True)
        print(f'Compiled {len(files)} production/fixture translation units; no link or execution')
        sys.exit(0)
    cmd += [str(p) for p in files]+['-o',str(d/'test')]
    subprocess.run(cmd, check=True)
    result = subprocess.run([str(d/'test'), str(R)], capture_output=True, timeout=60)
    if result.returncode:
        print(result.stderr.decode())
        result.check_returncode()
    # Baseline 9a209d8794d725393ddbb48d3880c03b9af29ba4: every emitted word,
    # X position, style, focus boundary and suffix position for all 360 cases.
    expected = 'f62831df0381e2682bb8669135d4da723e79bccb10d8fd8f631695b2ddef8397'
    assert hashlib.sha256(result.stdout).hexdigest() == expected, 'Baseline layout snapshot digest changed'
    if args.output: args.output.write_bytes(result.stdout)
    if args.compare: assert result.stdout == args.compare.read_bytes(), 'Layout text/position/style/focus snapshots changed'
    print(result.stderr.decode(), end='')
    print('EPUB prefix regression passed' + ('; all snapshots identical to baseline' if args.compare else ''))
