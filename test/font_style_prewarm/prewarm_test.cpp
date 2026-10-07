#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <Logging.h>
#include <SdCardFont.h>
#include <Utf8.h>
#include <builtinFonts/notosans_12_regular.h>
#include <builtinFonts/notosans_12_bold.h>
#include <builtinFonts/notosans_12_italic.h>
#include <builtinFonts/notosans_12_bolditalic.h>
#include <algorithm>
#include <iostream>
#include <map>
#include <vector>
#include <cstring>
#include <utility>

static void require(bool yes, const char* why) {
  if (!yes) { std::cerr << why << '\n'; std::exit(1); }
}
uint64_t probeDecompressCalls = 0, probeDecompressedBytes = 0;
bool probeFailDecompress = false;
static size_t allocAttempts = 0, failAllocation = 0;
static std::map<void*, size_t> allocations;
void* probeMalloc(size_t size) {
  if (++allocAttempts == failAllocation) return nullptr;
  void* p = std::malloc(size);
  if (p) allocations.emplace(p, size);
  return p;
}
void probeFree(void* p) {
  if (p) { require(allocations.erase(p) == 1, "unowned/double free"); std::free(p); }
}

static const EpdFontData* datas[] = {&notosans_12_regular, &notosans_12_bold,
                                    &notosans_12_italic, &notosans_12_bolditalic};
static EpdFont fonts[] = {EpdFont(datas[0]), EpdFont(datas[1]), EpdFont(datas[2]), EpdFont(datas[3])};
using Families = std::map<int, EpdFontFamily>;
static Families fullFamily() {
  return {{1, EpdFontFamily(&fonts[0], &fonts[1], &fonts[2], &fonts[3])}};
}
static std::string utf8(uint32_t c) {
  std::string s;
  if (c < 128) s += char(c);
  else if (c < 2048) { s += char(0xc0 | (c >> 6)); s += char(0x80 | (c & 63)); }
  else { s += char(0xe0 | (c >> 12)); s += char(0x80 | ((c >> 6) & 63)); s += char(0x80 | (c & 63)); }
  return s;
}
static std::string bodyText(int n) {
  std::string body; int count = 0;
  for (uint32_t c = 33; c < 0xffff && count < n; ++c) {
    bool all = true;
    for (auto* data : datas) {
      bool found = false;
      for (uint32_t i = 0; i < data->intervalCount; ++i) {
        if (c >= data->intervals[i].first && c <= data->intervals[i].last) found = true;
      }
      if (!found) all = false;
    }
    if (!all) continue;
    body += utf8(c); body += ' '; ++count;
  }
  require(count == n, "insufficient real font coverage");
  return body;
}

// Device/framebuffer setup fixture. The included function bodies below come
// verbatim from GfxRenderer.cpp, including differential rounding and clipping.
struct GfxRenderer {
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };
  enum Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };
  const Families& fontMap;
  FontCacheManager* fontCacheManager_;
  RenderMode renderMode = BW;
  Orientation orientation = LandscapeCounterClockwise;
  bool initialized = true;
  uint16_t panelWidth = 512, panelHeight = 256, panelWidthBytes = 64;
  std::vector<uint8_t> frame = std::vector<uint8_t>(512 * 256 / 8, 0xff);
  uint8_t* frameBuffer = frame.data();
  int getScreenWidth() const { return orientation == Portrait || orientation == PortraitInverted ? panelHeight : panelWidth; }
  int getScreenHeight() const { return orientation == Portrait || orientation == PortraitInverted ? panelWidth : panelHeight; }
  const uint8_t* getGlyphBitmap(const EpdFontData* d, const EpdGlyph* g) const {
    return fontCacheManager_->getDecompressor()->getBitmap(d, g, uint32_t(g - d->glyph));
  }
  void drawPixel(int, int, bool) const;
  void drawText(int, int, int, const char*, bool, EpdFontFamily::Style) const;
  void drawTextRotated90CW(int, int, int, const char*, bool, EpdFontFamily::Style) const;
  int getFontAscenderSize(int) const;
};
#include "raster.inc"

struct Draw { int id; uint8_t style; std::string text; };
using Draws = std::vector<Draw>;
struct Result {
  uint64_t calls = 0, bytes = 0;
  uint32_t cacheBytes = 0, lookupBytes = 0;
  size_t attempts = 0, misses = 0;
  std::map<std::pair<const EpdFontData*, uint32_t>, std::vector<uint8_t>> images;
  std::vector<std::vector<uint8_t>> frames;
};
static size_t cases = 0;
static Result run(const Families& family, const Draws& draws, int mode, size_t fail = 0, bool failGroup = false) {
  require(allocations.empty(), "previous scope leaked allocations");
  std::map<int, SdCardFont*> sd;
  FontDecompressor decomp; decomp.init();
  FontCacheManager manager(family, sd); manager.setFontDecompressor(&decomp);
  GfxRenderer renderer{family, &manager}; Result result;
  {
    auto scope = manager.createPrewarmScope();
    probeDecompressCalls = probeDecompressedBytes = allocAttempts = 0;
    failAllocation = fail; probeFailDecompress = failGroup;
    if (mode == 1) {
      for (const auto& d : draws)
        renderer.drawText(d.id, 0, 0, d.text.c_str(), true, EpdFontFamily::Style(d.style));
    } else if (mode == 2) {
      // Independent direct-prewarm control groups by actual font-data identity.
      std::map<const EpdFontData*, std::string> grouped;
      std::vector<const EpdFontData*> order;
      for (const auto& d : draws) {
        auto f = family.find(d.id); if (f == family.end() || d.id == 0) continue;
        auto* data = f->second.getData(EpdFontFamily::Style(d.style));
        if (!grouped.count(data)) order.push_back(data);
        grouped[data] += d.text;
      }
      for (auto* data : order) decomp.prewarmCache(data, grouped[data].c_str());
    }
    scope.endScanAndPrewarm();
    require(!manager.isScanning() && !manager.hasRecordedText(), "recording retained after prewarm");
    result.calls = probeDecompressCalls; result.bytes = probeDecompressedBytes;
    result.cacheBytes = decomp.getStats().pageBufferBytes;
    result.lookupBytes = decomp.getStats().pageGlyphsBytes;
    result.attempts = allocAttempts;
    failAllocation = 0; probeFailDecompress = false;
    scope.endScanAndPrewarm();
    require(result.attempts == allocAttempts && result.calls == probeDecompressCalls, "repeated end rescanned");
    for (const auto& d : draws) {
      auto f = family.find(d.id); if (f == family.end()) continue;
      auto st = EpdFontFamily::Style(d.style); auto* data = f->second.getData(st);
      const unsigned char* p = reinterpret_cast<const unsigned char*>(d.text.c_str());
      while (uint32_t cp = utf8NextCodepoint(&p)) {
        auto* glyph = f->second.getGlyph(cp, st); if (!glyph || !glyph->dataLength) continue;
        auto* bitmap = decomp.getBitmap(data, glyph, uint32_t(glyph - data->glyph));
        require(bitmap, "missing real glyph bitmap");
        result.images[{data, cp}] = std::vector<uint8_t>(bitmap, bitmap + glyph->dataLength);
      }
    }
    for (int orientation = 0; orientation < 4; ++orientation) for (int mode = 0; mode < 3; ++mode) {
      renderer.orientation = GfxRenderer::Orientation(orientation);
      renderer.renderMode = GfxRenderer::RenderMode(mode);
      std::fill(renderer.frame.begin(), renderer.frame.end(), mode == 1 ? 0 : 0xff);
      int y = -3;
      for (const auto& d : draws) {
        renderer.drawText(d.id, -7, y, d.text.c_str(), mode != 1, EpdFontFamily::Style(d.style));
        renderer.drawTextRotated90CW(d.id, 160 + y, 190, d.text.c_str(), mode != 1, EpdFontFamily::Style(d.style));
        y += 42;
      }
      result.frames.push_back(renderer.frame);
    }
    result.misses = decomp.getStats().cacheMisses;
  }
  require(!manager.isScanning() && !manager.hasRecordedText() && allocations.empty(), "scope cleanup failed");
  decomp.clearCache(); require(allocations.empty(), "repeated cleanup failed");
  ++cases; return result;
}
static void equalOutput(const Result& a, const Result& b) {
  require(a.images == b.images, "requested glyph bytes differ");
  require(a.frames == b.frames, "production renderer framebuffer differs");
}
static Draws workload(int n, int styles, int repeat) {
  auto body = bodyText(n); std::string full;
  for (int i = 0; i < repeat; ++i) full += body;
  Draws draws = {{1, 0, full}};
  for (int i = 1; i < styles; ++i) draws.push_back({1, uint8_t(i), std::string(1, char('A' + i - 1))});
  return draws;
}
static void contracts() {
  auto family = fullFamily(); std::map<int, SdCardFont*> sd;
  FontDecompressor decomp; FontCacheManager manager(family, sd); manager.setFontDecompressor(&decomp);
  { auto scope = manager.createPrewarmScope();
    manager.recordText(nullptr, 1, EpdFontFamily::BOLD);
    manager.recordText("", 1, EpdFontFamily::REGULAR);
    manager.recordText("A", 0, EpdFontFamily::ITALIC);
    require(!manager.hasRecordedText(), "invalid input recorded");
    manager.recordText("A", 987, EpdFontFamily::ITALIC);
    scope.endScanAndPrewarm(); require(allocations.empty(), "unknown font allocated cache");
  }
  { auto original = manager.createPrewarmScope();
    manager.recordText("ffi a\xcc\x81", 1, EpdFontFamily::REGULAR);
    auto moved = std::move(original); // destructor-only preparation and cleanup
    require(manager.isScanning(), "scope move changed scan mode");
  }
  require(allocations.empty() && !manager.isScanning() && !manager.hasRecordedText(), "moved scope cleanup failed");
  SdCardFont sdFont; sd.emplace(9, &sdFont);
  { auto scope = manager.createPrewarmScope();
    manager.recordText("abc", 9, EpdFontFamily::BOLD);
    manager.recordText("def", 9, EpdFontFamily::Style(6));
    manager.recordText("ghi", 9, EpdFontFamily::REGULAR);
    scope.endScanAndPrewarm();
    require(sdFont.calls.size() == 1 && sdFont.calls[0].text == "abcdefghi" && sdFont.calls[0].mask == 7,
            "SD aggregation text/order/mask changed");
  }
  require(sdFont.clears == 2 && sdFont.resets == 1, "SD lifecycle changed");
  { auto scope = manager.createPrewarmScope();
    manager.recordText("\x80", 9, EpdFontFamily::ITALIC);
    scope.endScanAndPrewarm();
    require(sdFont.calls.size() == 2 && sdFont.calls[1].text == "\x80" && sdFont.calls[1].mask == 1,
            "SD malformed-byte style fallback changed");
  }
  FontCacheManager noDecompressor(family, sd);
  { auto scope = noDecompressor.createPrewarmScope(); noDecompressor.recordText("hello", 1, EpdFontFamily::BOLD); }
  require(!noDecompressor.hasRecordedText(), "no-decompressor cleanup failed");
}
int main() {
  const auto family = fullFamily();
  for (int n : {32, 64, 128, 256}) for (int styles : {1, 2, 4}) for (int repeat : {1, 4}) {
    auto draws = workload(n, styles, repeat);
    auto actual = run(family, draws, 1), control = run(family, draws, 2);
    equalOutput(actual, control);
#ifdef ENFORCE_COST
    require(actual.cacheBytes == control.cacheBytes && actual.lookupBytes == control.lookupBytes &&
            actual.calls == control.calls && actual.bytes == control.bytes, "unused style glyphs were prewarmed");
    require(actual.misses == 0, "requested glyph missed style-specific cache");
#endif
    std::cout << "workload n=" << n << " styles=" << styles << " repeat=" << repeat
              << " cache=" << actual.cacheBytes + actual.lookupBytes << " control=" << control.cacheBytes + control.lookupBytes
              << " groups=" << actual.calls << " control_groups=" << control.calls << '\n';
  }
  // Ligatures, chaining, combining marks, whitespace, unsupported codepoints,
  // underline/high style bits, and separated calls into the same actual font.
  Draws text = {{1, 4, "AV ffi ffl fi fl a\xcc\x81 e\xcc\x88"}, {1, 5, "of"}, {1, 7, "fice"},
                {1, 6, "\xce\xa9 \xe6\xbc\xa2"}, {1, 0x84, "repeat ffi"}, {1, 3, " \t "}};
  EpdFont sameData(datas[0]);
  std::vector<Families> variants = {family,
    {{1, EpdFontFamily(&fonts[0])}},
    {{1, EpdFontFamily(&fonts[0], &fonts[1])}},
    {{1, EpdFontFamily(&fonts[0], nullptr, &fonts[2])}},
    {{1, EpdFontFamily(&fonts[0], &sameData, &fonts[2], &fonts[2])}}};
  for (const auto& f : variants) {
    auto actual = run(f, text, 1), control = run(f, text, 2), uncached = run(f, text, 0);
    equalOutput(actual, control); equalOutput(actual, uncached);
#ifdef ENFORCE_COST
    require(actual.cacheBytes == control.cacheBytes && actual.lookupBytes == control.lookupBytes, "font alias duplicated a slot");
#endif
  }
  // Same glyph indices in distinct fonts and more font identities than slots.
  auto many = family;
  EpdFontData identityCopy = notosans_12_regular; EpdFont separate(&identityCopy);
  many.emplace(2, EpdFontFamily(&separate)); many.emplace(3, EpdFontFamily(&fonts[2]));
  auto crowded = workload(600, 4, 1);
  crowded.push_back({2, 0, "ffi another identity"}); crowded.push_back({3, 7, "italic again"});
  equalOutput(run(many, crowded, 1), run(many, crowded, 0));
  auto draws = workload(128, 4, 1); auto good = run(family, draws, 1);
  for (size_t failure = 1; failure <= good.attempts; ++failure) {
    equalOutput(run(family, draws, 1, failure), good);
    equalOutput(run(family, draws, 1), good); // next page retries normally
  }
  equalOutput(run(family, draws, 1, 0, true), good);
  equalOutput(run(family, draws, 1), good);
  contracts();
  std::cout << "PASS cases=" << cases << " raster_frames=" << cases * 12
            << " allocation_faults=" << good.attempts << " cleanup_live=" << allocations.size() << '\n';
}
