#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Utf8.h>
#include <algorithm>
#include <cassert>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>
#include "Epub/blocks/BlockStyle.h"
#define private public
#include "Epub/blocks/TextBlock.h"
#undef private
#include "Epub/ParsedText.h"
#include "Epub/hyphenation/Hyphenator.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include "builtinFonts/notosans_12_regular.h"
#pragma GCC diagnostic pop

[[maybe_unused]] static std::string clean(std::string s) {
  for (size_t i; (i = s.find("\xc2\xad")) != std::string::npos;) s.erase(i, 2);
  return s;
}
static std::string repeat(const std::string& s, size_t n) {
  std::string result;
  while (n--) result += s;
  return result;
}
#ifndef BASELINE
static size_t checks = 0;
static void verifyMetrics(const GfxRenderer& renderer, int id, const std::string& text, EpdFontFamily::Style style) {
  GfxRenderer::TextPrefixMetrics metrics;
  if (!renderer.getTextPrefixMetrics(id, text, metrics, style)) { std::cerr << "Rejected id=" << id << " style=" << style << " bytes=" << text.size() << " text=" << text << std::endl; assert(false); }
  const auto* begin = reinterpret_cast<const uint8_t*>(text.c_str());
  const auto* p = begin;
  while (utf8NextCodepoint(&p)) {
    size_t offset = p - begin;
    auto prefix = clean(text.substr(0, offset));
    assert(metrics.plain[offset] == static_cast<uint16_t>(renderer.getTextAdvanceX(id, prefix.c_str(), style)));
    prefix += '-';
    assert(metrics.hyphenated[offset] == static_cast<uint16_t>(renderer.getTextAdvanceX(id, prefix.c_str(), style)));
    checks += 2;
  }
}
#endif
int main(int argc, char** argv) {
  assert(argc == 2); Storage.root = argv[1];
  EpdFont font(&notosans_12_regular);
  GfxRenderer r; r.fontMap.emplace(1, EpdFontFamily(&font));
  SdCardFont sd; assert(sd.load("/SD_fonts/SourceHanSansSC/SourceHanSansSC_14.cpfont"));
  r.sdCardFonts_.emplace(2, &sd); r.fontMap.emplace(2, EpdFontFamily(sd.getEpdFont(0)));
  const std::string cjk = "天地玄黄宇宙洪荒日月盈昃辰宿列张寒来暑往秋收冬藏闰余成岁律吕调阳云腾致雨露结为霜金生丽水玉出昆冈剑号巨阙珠称夜光果珍李柰菜重芥姜";
  const std::vector<std::string> words = {std::string(200, 'W'), repeat("ACGT", 49), repeat(cjk, 2).substr(0, 198),
    repeat("ffi", 60), repeat("AV", 90), repeat("ab\xc2\xad" "le", 20), repeat("a\xcc\x81", 40),
    repeat("\xd7\x90\xd7\x91", 40), "US-Satellitensystems", "all'improvviso", "hello", " ",
    repeat("f\xcc\x81" "fi", 20), std::string(201, 'W'), std::string("bad\xff", 4), std::string("bad\0tail", 8)};
#ifndef BASELINE
  // Custom table stresses non-additive metrics, truncation in chained ligatures,
  // ligatures with the appended hyphen/combining mark, missing glyphs and negative kerning.
  std::vector<EpdGlyph> glyphs(128);
  for (size_t i = 0; i < glyphs.size(); ++i) glyphs[i].advanceX = 20 + (i * 13) % 197;
  const EpdUnicodeInterval interval{0, 127, 0};
  const EpdKernClassEntry classes[] = {{'-', 1}, {'A', 1}, {'V', 2}, {'f', 2}, {'i', 1}, {'x', 2}, {'y', 1}};
  const int8_t kern[] = {-120, 71, -57, 119};
  const EpdLigaturePair ligatures[] = {{('f' << 16) | '-', 'x'}, {('f' << 16) | 'f', 'x'},
      {('f' << 16) | 'i', 'y'}, {('f' << 16) | 0x301, 'x'}, {('x' << 16) | '-', 'y'},
      {('x' << 16) | 'i', 'y'}, {('y' << 16) | 'f', 'x'}};
  EpdFontData custom{}; custom.glyph = glyphs.data(); custom.intervals = &interval; custom.intervalCount = 1;
  custom.kernLeftClasses = custom.kernRightClasses = classes; custom.kernMatrix = kern;
  custom.kernLeftEntryCount = custom.kernRightEntryCount = 7;
  custom.kernLeftClassCount = custom.kernRightClassCount = 2;
  custom.ligaturePairs = ligatures; custom.ligaturePairCount = 7;
  EpdFont synthetic(&custom); r.fontMap.emplace(3, EpdFontFamily(&font, &synthetic, &synthetic));
  for (const auto& word : words) {
    if (word.size() > 200 || word.find('\xff') != std::string::npos || word.find('\0') != std::string::npos) continue;
    for (int id : {1, 3}) for (int style = 0; style < 8; ++style)
      verifyMetrics(r, id, word, static_cast<EpdFontFamily::Style>(style));
  }
  uint32_t random = 91827;
  const std::vector<std::string> alphabet = {"f", "i", "A", "V", "-", "x", "y", "\xcc\x81", "\xc2\xad", "\xd7\x90"};
  for (int sample = 0; sample < 128; ++sample) {
    std::string word;
    for (int n = 0; n < 50; ++n) { random = random * 1664525u + 1013904223u; word += alphabet[random % alphabet.size()]; }
    verifyMetrics(r, 3, word, EpdFontFamily::BOLD);
  }
  GfxRenderer::TextPrefixMetrics table;
  assert(!r.getTextPrefixMetrics(1, "", table));
  assert(!r.getTextPrefixMetrics(99, "abc", table));
  assert(!r.getTextPrefixMetrics(1, std::string(201, 'W'), table));
  assert(!r.getTextPrefixMetrics(1, std::string("abc\0d", 5), table));
  for (const auto& bad : {std::string("\xff"), std::string("\xe2\x82"), std::string("\xc0\xaf"), std::string("\xed\xa0\x80")})
    assert(!r.getTextPrefixMetrics(1, bad, table));
  // Failed lazy/admission paths retain no state; an ordinary retry succeeds.
  EpdFontData lazy = custom;
  lazy.glyphMissHandler = [](void*, uint32_t) -> const EpdGlyph* { assert(false); return nullptr; };
  EpdFont lazyFont(&lazy); r.fontMap.emplace(4, EpdFontFamily(&lazyFont));
  assert(!r.getTextPrefixMetrics(4, "ffi", table));
  assert(r.getTextPrefixMetrics(1, "ffi", table));
  assert(!r.getTextPrefixMetrics(2, cjk, table)); // No advance table yet; no speculative SD read.
  r.ensureSdCardFontReady(2, {cjk}, true, 15);
  verifyMetrics(r, 2, cjk, EpdFontFamily::BOLD_ITALIC);
  assert(!r.getTextPrefixMetrics(2, "\xf0\x9f\x98\x80", table)); // Missing/uncached advance.
  verifyMetrics(r, 2, cjk, EpdFontFamily::REGULAR); // Retry after miss.
  sd.clearPersistentCache();
  assert(!r.getTextPrefixMetrics(2, cjk, table));
  assert(sd.load("/SD_fonts/SourceHanSansSC/SourceHanSansSC_14.cpfont"));
  r.ensureSdCardFontReady(2, {cjk}, true, 15); verifyMetrics(r, 2, cjk, EpdFontFamily::REGULAR);
#endif
  size_t layouts = 0;
  for (size_t wi = 0; wi < words.size(); ++wi) for (bool hyp : {false, true}) for (bool focus : {false, true})
    for (int style : {0, 1, 6}) for (int viewport : {400, 600}) {
      if (words[wi].find('\0') != std::string::npos) continue; // Existing addWord NUL behavior is outside this optimization.
      Hyphenator::setPreferredLanguage(wi == 2 ? "zh" : "en");
      ParsedText paragraph(true, hyp, focus); int id = wi == 2 ? 2 : 1;
      paragraph.addWord(words[wi], static_cast<EpdFontFamily::Style>(style));
      paragraph.addWord("tail", EpdFontFamily::REGULAR, true, false);
      std::vector<std::shared_ptr<TextBlock>> lines;
      measure_calls = measure_codepoints = prefix_calls = 0;
      paragraph.layoutAndExtractLines(r, id, viewport, [&](auto line) { lines.push_back(line); });
      assert(paragraph.isEmpty());
      if (wi == 10 || wi == 11 || wi == 14) assert(prefix_calls == 0);
      if (wi == 0 && !focus && style == 0 && viewport == 400) {
        std::cerr << "W200 calls=" << measure_calls << " decodes=" << measure_codepoints << '\n';
#if !defined(BASELINE) || defined(ENFORCE_COST)
        assert(measure_codepoints < 6000); // Original: 87,958, independent of wall-clock timing.
#endif
      }
      if (wi == 2 && !focus && style == 0 && viewport == 400) {
        std::cerr << "CJK66 calls=" << measure_calls << " decodes=" << measure_codepoints << '\n';
#if !defined(BASELINE) || defined(ENFORCE_COST)
        assert(measure_codepoints < 1000); // Original: 4,946.
#endif
      }
      std::cout << wi << '/' << hyp << '/' << focus << '/' << style << '/' << viewport << ':';
      for (const auto& line : lines) {
        for (size_t i = 0; i < line->words.size(); ++i)
          std::cout << std::quoted(line->words[i]) << '@' << line->wordXpos[i] << '#' << static_cast<int>(line->wordStyles[i]) << ';';
        std::cout << "focus:"; for (auto x : line->wordFocusBoundary) std::cout << static_cast<int>(x) << ',';
        std::cout << "suffix:"; for (auto x : line->wordFocusSuffixX) std::cout << x << ',';
        std::cout << '|';
      }
      std::cout << '\n'; ++layouts;
    }
#ifndef BASELINE
  std::cerr << "prefix comparisons=" << checks << '\n';
#endif
  std::cerr << "layout snapshots=" << layouts << '\n';
}
