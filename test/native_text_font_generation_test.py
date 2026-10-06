#!/usr/bin/env python3
"""Exercise production font-registration mutation bodies and exhaustion policy."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
header = (ROOT / 'lib/GfxRenderer/GfxRenderer.h').read_text()
source = (ROOT / 'lib/GfxRenderer/GfxRenderer.cpp').read_text()

def function(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth = 0
    for end in range(brace, len(text)):
        depth += (text[end] == '{') - (text[end] == '}')
        if not depth:
            return text[start:end + 1]
    raise AssertionError(signature)

fixture = r'''
#include <cassert>
#include <cstdint>
#include <map>
#define LOG_ERR(...) ((void)0)
struct EpdFontFamily { int value = 0; };
struct SdCardFont {};
class GfxRenderer {
 public:
  std::map<int,EpdFontFamily> fontMap;
  std::map<int,SdCardFont*> sdCardFonts_;
'''
field = next(line.strip() for line in header.splitlines() if 'uint64_t fontLayoutGeneration_ =' in line)
fixture += field + '\n'
for signature in ['void invalidateFontLayout()', 'void removeFont(', 'uint64_t getFontLayoutGeneration()',
                  'void registerSdCardFont(', 'void unregisterSdCardFont(', 'void clearSdCardFonts()']:
    fixture += function(header, signature) + '\n'
fixture += 'void insertFont(int,EpdFontFamily);\n};\n'
fixture += function(source, 'void GfxRenderer::insertFont(')
fixture += r'''
int main() {
  GfxRenderer r; SdCardFont sd;
  assert(r.getFontLayoutGeneration() == 1);
  r.insertFont(10,{1}); assert(r.getFontLayoutGeneration() == 2);
  r.insertFont(10,{2}); assert(r.getFontLayoutGeneration() == 2 && r.fontMap.at(10).value == 1);
  r.removeFont(10); assert(r.getFontLayoutGeneration() == 3 && r.fontMap.empty());
  r.registerSdCardFont(10,&sd); assert(r.getFontLayoutGeneration() == 4 && r.sdCardFonts_.at(10) == &sd);
  r.clearSdCardFonts(); assert(r.getFontLayoutGeneration() == 5 && r.sdCardFonts_.empty());
  r.unregisterSdCardFont(10); assert(r.getFontLayoutGeneration() == 6);
  r.removeFont(99); assert(r.getFontLayoutGeneration() == 7);
  r.fontLayoutGeneration_ = UINT64_MAX;
  r.removeFont(99); assert(r.getFontLayoutGeneration() == 0);
  r.insertFont(10,{3}); r.registerSdCardFont(10,&sd); r.clearSdCardFonts(); r.removeFont(10);
  assert(r.getFontLayoutGeneration() == 0);
}
'''
with tempfile.TemporaryDirectory(prefix='native-font-generation-') as tmp:
    path = Path(tmp)
    (path / 'test.cpp').write_text(fixture)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Production font insertion/removal/SD registration and exhausted-generation invalidation PASS')
