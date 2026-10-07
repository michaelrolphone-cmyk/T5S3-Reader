#!/usr/bin/env python3
"""Real SD-font loader and shipped CJK font; verbatim renderer measurements.

Only storage/display boundaries are fixtures. SD_FONT_TEST_ROOT can select the
original source tree for a negative control without copying production logic.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(os.environ.get('SD_FONT_TEST_ROOT', Path(__file__).resolve().parents[2]))
source = (ROOT / 'lib/GfxRenderer/GfxRenderer.cpp').read_text()
def function(signature):
    start = source.index(signature)
    brace = source.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

prefix = r'''
#include <SdCardFont.h>
#include <EpdFontFamily.h>
#include <Utf8.h>
#include <Logging.h>
#include <HalStorage.h>
#include <map>
#include <string>
#include <iostream>
#include <cassert>
struct GfxRenderer {
  std::map<int, SdCardFont*> sdCardFonts_;
  std::map<int, EpdFontFamily> fontMap;
  int getTextAdvanceX(int, const char*, EpdFontFamily::Style) const;
  int getSpaceWidth(int, EpdFontFamily::Style) const;
  int getSpaceAdvance(int, uint32_t, uint32_t, EpdFontFamily::Style) const;
};
'''
body = function('uint8_t resolveSdCardStyle(')
if 'uint16_t sdCardAdvance(' in source:
    body += function('uint16_t sdCardAdvance(')
for signature in ('int GfxRenderer::getTextAdvanceX(', 'int GfxRenderer::getSpaceWidth(',
                  'int GfxRenderer::getSpaceAdvance('):
    body += function(signature)

tests = r'''
std::string utf8(unsigned cp) {
  if (cp < 128) return std::string(1, char(cp));
  std::string s;
  if (cp < 2048) { s += char(0xc0 | (cp >> 6)); s += char(0x80 | (cp & 63)); }
  else { s += char(0xe0 | (cp >> 12)); s += char(0x80 | ((cp >> 6) & 63)); s += char(0x80 | (cp & 63)); }
  return s;
}
std::string sequence(unsigned start, unsigned count) {
  std::string s;
  for (unsigned i = 0; i < count; ++i) s += utf8(start + i);
  return s;
}
int main(int argc, char** argv) {
  assert(argc == 2);
  Storage.root = argv[1];
  const std::string root = Storage.root;
  for (bool incremental : {false, true}) {
    SdCardFont font;
    assert(font.load("/SD_fonts/SourceHanSansSC/SourceHanSansSC_14.cpfont"));
    GfxRenderer r;
    r.sdCardFonts_.emplace(1, &font);
    r.fontMap.emplace(1, EpdFontFamily(font.getEpdFont(0), font.getEpdFont(1),
                                     font.getEpdFont(2), font.getEpdFont(3)));
    for (auto style : {EpdFontFamily::REGULAR, EpdFontFamily::BOLD,
                       EpdFontFamily::ITALIC, EpdFontFamily::BOLD_ITALIC}) {
      font.clearCache(); font.clearPersistentCache();
      const auto resolved = font.resolveStyle(static_cast<uint8_t>(style));
      auto* epd = font.getEpdFont(resolved);
      assert(epd);
      const auto mask = uint8_t(1u << resolved);
      if (incremental) {
        font.buildAdvanceTable(sequence(0x4e00, 600).c_str(), mask);
        font.clearCache();
        font.buildAdvanceTable(sequence(0x4e00 + 600, 200).c_str(), mask);
      } else {
        font.buildAdvanceTable(sequence(0x4e00, 800).c_str(), mask);
      }
      assert(font.hasAdvanceTable());
      const auto* glyph = epd->getGlyph(0x4e00 + 799);
      assert(glyph && glyph->advanceX > 0);
      int expected = fp4::toPixel(glyph->advanceX);
      int actual = r.getTextAdvanceX(1, utf8(0x4e00 + 799).c_str(), style);
      if (expected != actual) {
        std::cerr << "covered glyph after cache saturation: expected " << expected
                  << ", got " << actual << '\n';
        return 1;
      }
      // Layout retains its advance-only sum, including mixed hit/miss strings.
      const std::string mixed = utf8(0x4e00) + utf8(0x4e00 + 799) + " ";
      uint32_t sum = 0;
      for (unsigned cp : {0x4e00u, 0x4e00u + 799, 32u}) {
        const auto* g = epd->getGlyph(cp); assert(g); sum += g->advanceX;
      }
      assert(r.getTextAdvanceX(1, mixed.c_str(), style) == fp4::toPixel(sum));
      const auto* space = epd->getGlyph(' '); assert(space);
      const int spacePixels = fp4::toPixel(space->advanceX);
      assert(r.getSpaceWidth(1, style) == spacePixels);
      assert(r.getSpaceAdvance(1, 'A', 'V', style) == spacePixels);
      assert(r.getTextAdvanceX(1, "", style) == 0);
      assert(r.getTextAdvanceX(1, "\xef\xbf\xbf", style) == 0); // absent U+FFFF
      // A failed fallback read does not poison the cache; retry remains possible.
      font.clearCache();
      Storage.root = root + "/missing";
      const auto retryText = utf8(0x4e00 + 950);
      assert(r.getTextAdvanceX(1, retryText.c_str(), style) == 0);
      Storage.root = root;
      const auto* retryGlyph = epd->getGlyph(0x4e00 + 950);
      assert(retryGlyph && retryGlyph->advanceX);
      assert(r.getTextAdvanceX(1, retryText.c_str(), style) == fp4::toPixel(retryGlyph->advanceX));
      font.clearCache();
      assert(r.getTextAdvanceX(1, utf8(0x4e00 + 799).c_str(), style) == expected);
    }
    // Cache misses and true zero-width glyphs have distinct lookup results.
#ifdef HAS_TRY_ADVANCE
    font.clearCache(); font.clearPersistentCache();
    assert(font.buildAdvanceTable("\xe3\x80\xaa", 1) >= 0); // zero-width ideographic level tone mark
    uint16_t advance = 1234;
    assert(font.tryGetAdvance(0x302a, 0, advance) && advance == 0);
    advance = 1234;
    assert(!font.tryGetAdvance(0xffff, 0, advance) && advance == 1234);
    assert(!font.tryGetAdvance(0x302a, 1, advance) && advance == 1234);
    Storage.root = root + "/missing";
    assert(r.getTextAdvanceX(1, "\xe3\x80\xaa", EpdFontFamily::REGULAR) == 0);
    Storage.root = root;
    font.clearPersistentCache();
    assert(!font.tryGetAdvance(0x302a, 0, advance));
#endif
    assert(r.getSpaceWidth(999, EpdFontFamily::REGULAR) == 0);
    assert(r.getTextAdvanceX(999, "text", EpdFontFamily::REGULAR) == 0);
  }
  std::cout << "SD font advance cache saturation/style/zero/miss/retry tests passed\n";
}
'''
with tempfile.TemporaryDirectory(prefix='sd-font-advance-') as directory:
    temp = Path(directory)
    cpp = temp / 'test.cpp'
    cpp.write_text(prefix + body + tests)
    command = ['c++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror']
    if 'tryGetAdvance' in (ROOT / 'lib/EpdFont/SdCardFont.h').read_text():
        command += ['-DHAS_TRY_ADVANCE']
    if os.environ.get('SD_FONT_SANITIZE') == '1':
        command += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all']
    command += ['-I' + str(ROOT / p) for p in ('test/fontawesome/stubs', 'lib/EpdFont', 'lib/Utf8')]
    command += [str(cpp)] + [str(ROOT / p) for p in (
        'lib/EpdFont/SdCardFont.cpp', 'lib/EpdFont/EpdFont.cpp',
        'lib/EpdFont/EpdFontFamily.cpp', 'lib/Utf8/Utf8.cpp')]
    command += ['-o', str(temp / 'test')]
    subprocess.run(command, check=True, timeout=90)
    subprocess.run([str(temp / 'test'), str(ROOT)], check=True, timeout=60)
