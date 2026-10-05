#!/usr/bin/env python3
"""Production font cache/decompressor and raster regression, with allocation fault injection."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--baseline-ref')
p.add_argument('--sanitize', action='store_true')
p.add_argument('--enforce-cost', action='store_true')
args = p.parse_args()


def function(text, signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


with tempfile.TemporaryDirectory(prefix='font-style-prewarm-') as tmp:
    out = Path(tmp)
    (out/'Arduino.h').write_text('#pragma once\n#include <cstdint>\n#include <cstring>\ninline uint32_t millis(){return 0;}\ninline uint32_t micros(){return 0;}\n')
    (out/'Logging.h').write_text('#pragma once\ntemplate<class... T> inline void fixtureLog(T&&...){}\n#define LOG_DBG(...) fixtureLog(__VA_ARGS__)\n#define LOG_ERR(...) fixtureLog(__VA_ARGS__)\n')
    # SD preparation is intentionally unchanged; verify exact text/mask/order at
    # its existing interface, without claiming a filesystem/rendering SD test.
    (out/'SdCardFont.h').write_text('''#pragma once
#include <cstdint>
#include <string>
#include <vector>
class SdCardFont { public:
 struct Call {std::string text; uint8_t mask;};
 std::vector<Call> calls; int clears=0, resets=0;
 void clearCache(){++clears;}
 int prewarm(const char* s,uint8_t m){calls.push_back({s,m});return 0;}
 void logStats(const char*){} void resetStats(){++resets;}
};
''')
    for name in ('FontCacheManager.cpp', 'FontCacheManager.h'):
        path = 'lib/GfxRenderer/' + name
        text = subprocess.check_output(['git', '-C', str(ROOT), 'show', args.baseline_ref+':'+path], text=True) if args.baseline_ref else (ROOT/path).read_text()
        (out/name).write_text(text)
    decompressor = (ROOT/'lib/EpdFont/FontDecompressor.cpp').read_text()
    decompressor = decompressor.replace('#include <cstdlib>', '#include <cstdlib>\nextern uint64_t probeDecompressCalls, probeDecompressedBytes;\nextern bool probeFailDecompress;\nvoid* probeMalloc(size_t);\nvoid probeFree(void*);')
    signature = 'bool FontDecompressor::decompressGroup('
    pos = decompressor.index('  const EpdFontGroup& group =', decompressor.index(signature))
    decompressor = decompressor[:pos] + '  ++probeDecompressCalls; probeDecompressedBytes += outSize;\n  if (probeFailDecompress) { probeFailDecompress=false; return false; }\n' + decompressor[pos:]
    decompressor = decompressor.replace('malloc(', 'probeMalloc(').replace('free(', 'probeFree(')
    (out/'FontDecompressor.cpp').write_text(decompressor)
    gfx = (ROOT/'lib/GfxRenderer/GfxRenderer.cpp').read_text()
    # Exact production layout, glyph painting and pixel orientation. Only the
    # framebuffer/device setup and getGlyphBitmap's hardware-independent dispatch
    # are fixtures. No alternate text layout or raster algorithm is used.
    renderer = function(gfx, 'static inline void rotateCoordinates(')
    renderer += '\nenum class TextRotation { None, Rotated90CW };\n'
    renderer += function(gfx, 'template <TextRotation rotation>')
    for signature in ('void GfxRenderer::drawPixel(', 'void GfxRenderer::drawText(', 'void GfxRenderer::drawTextRotated90CW(', 'int GfxRenderer::getFontAscenderSize('):
        renderer += '\n' + function(gfx, signature)
    (out/'raster.inc').write_text(renderer)
    flags = ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers', '-g', '-O1', '-ffunction-sections', '-fdata-sections', '-include', 'cstdlib']
    san = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer'] if args.sanitize else []
    if args.enforce_cost: flags += ['-DENFORCE_COST']
    inc = ['-I'+str(out), '-I'+str(ROOT/'lib/EpdFont'), '-I'+str(ROOT/'lib/Utf8')]
    obj = out/'inflate.o'
    subprocess.run(['cc', '-std=c11', '-g', '-O1', '-ffunction-sections', '-fdata-sections', *san, '-c', str(ROOT/'lib/uzlib/src/tinflate.c'), '-o', str(obj)], check=True, timeout=90)
    units = [ROOT/'test/font_style_prewarm/prewarm_test.cpp', out/'FontCacheManager.cpp', out/'FontDecompressor.cpp']
    units += [ROOT/('lib/EpdFont/'+n+'.cpp') for n in ('EpdFont', 'EpdFontFamily')]
    units += [ROOT/'lib/InflateReader/InflateReader.cpp', ROOT/'lib/Utf8/Utf8.cpp']
    binary = out/'test'
    subprocess.run([os.environ.get('CXX', 'c++'), *flags, *san, *inc, *map(str, units), str(obj), '-Wl,--gc-sections', '-o', str(binary)], check=True, timeout=120)
    subprocess.run([str(binary)], check=True, timeout=120)
