#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

struct Rect { int x, y, width, height; };
struct RecentBook { std::string path, title, author, coverBmpPath; };
struct ThemeMetrics {
  int homeRecentBooksCount = 1, contentSidePadding = 20, homeCoverHeight = 300;
  bool homeContinueReadingInMenu = false;
};
enum class TextRole { System, UserContent };
struct EpdFontFamily { enum Style { REGULAR, BOLD }; };
struct TestState {
  bool open = true, parse = true;
  int width = 240, height = 360, opens = 0, closes = 0, delegates = 0, contentHeight = 24;
};
inline TestState state;
struct FsFile { void close() { ++state.closes; } };
struct StorageMock {
  bool openFileForRead(const char*, const std::string&, FsFile&) { ++state.opens; return state.open; }
};
inline StorageMock Storage;
enum class BmpReaderError { Ok, Bad };
struct Bitmap {
  explicit Bitmap(FsFile&) {}
  BmpReaderError parseHeaders() { return state.parse ? BmpReaderError::Ok : BmpReaderError::Bad; }
  int getWidth() const { return state.width; }
  int getHeight() const { return state.height; }
};
struct HalDisplay { enum RefreshMode { FULL_REFRESH, HALF_REFRESH, BALANCED_REFRESH, FAST_REFRESH }; };
class GfxRenderer {
 public:
  struct Text { int x, y, font; std::string value; bool black; };
  int width, height;
  bool reverse;
  mutable int pixels = 0, bitmaps = 0, coverBases = 0, presentations = 0, grayPresentations = 0;
  int failure = 0; // 1=snapshot, 2=base, 3=LSB, 4=MSB allocation failure
  mutable std::vector<Text> texts;
  mutable Rect coverBase{}, bitmapRect{};
  mutable std::vector<uint8_t> frame, saved, base, lsb, msb, shown;
  mutable std::vector<std::string> calls;
  explicit GfxRenderer(int w = 540, int h = 960, bool flipped = false)
      : width(w), height(h), reverse(flipped), frame((w * h + 7) / 8, 0xff) {}
  int getScreenWidth() const { return width; }
  int getScreenHeight() const { return height; }
  size_t index(int x, int y) const {
    assert(x >= 0 && y >= 0 && x < width && y < height);
    const size_t i = static_cast<size_t>(y) * width + x;
    return reverse ? static_cast<size_t>(width) * height - 1 - i : i;
  }
  bool bit(const std::vector<uint8_t>& b, int x, int y) const {
    const size_t i = index(x, y);
    return (b.at(i / 8) & (0x80u >> (i % 8))) != 0;
  }
  void drawPixel(int x, int y, bool black) const {
    const size_t i = index(x, y);
    const uint8_t mask = static_cast<uint8_t>(0x80u >> (i % 8));
    if (black) frame[i / 8] &= static_cast<uint8_t>(~mask); else frame[i / 8] |= mask;
    ++pixels;
  }
  void fillRect(int x, int y, int w, int h, bool black) const {
    assert(w > 0 && h > 0);
    coverBase = {x, y, w, h}; ++coverBases;
    for (int j = y; j < y + h; ++j) for (int i = x; i < x + w; ++i) drawPixel(i, j, black);
  }
  void drawBitmap(const Bitmap&, int x, int y, int w, int h) const {
    assert(coverBases > 0 && coverBase.x == x && coverBase.y == y && coverBase.width == w && coverBase.height == h);
    assert(w <= state.width && h <= state.height && w > 0 && h > 0);
    // Like production BMP drawing, leave white pixels alone. Black samples at
    // the top and left of the art catch accidental reflection-plane overlap.
    for (int j = y; j < y + h; ++j) for (int i = x; i < x + w; ++i) {
      if ((i - x) % 9 < 4) drawPixel(i, j, true);
    }
    bitmapRect = {x, y, w, h}; ++bitmaps;
  }
  void drawRect(int x, int y, int w, int h, bool black) const {
    for (int i = x; i < x + w; ++i) { drawPixel(i, y, black); drawPixel(i, y + h - 1, black); }
    for (int j = y; j < y + h; ++j) { drawPixel(x, j, black); drawPixel(x + w - 1, j, black); }
  }
  int getTextWidth(int, const char* text) const { return static_cast<int>(std::strlen(text)) * 6; }
  int getLineHeight(int) const { return 20; }
  std::string truncatedText(int, const char* text, int w) const {
    return std::string(text).substr(0, static_cast<size_t>(std::max(0, w / 6)));
  }
  void drawText(int font, int x, int y, const char* text, bool black) const {
    assert(!black && x >= 0 && y >= 0 && x + getTextWidth(font, text) <= width && y + 20 <= height);
    texts.push_back({x, y, font, text, black});
    // Deterministic white glyph samples, not a substitute font preview.
    for (int i = 0; i < getTextWidth(font, text); ++i) drawPixel(x + i, y + (i % 11), black);
  }
  void clearScreen(uint8_t color = 0xff) const { std::fill(frame.begin(), frame.end(), color); calls.push_back("clear"); }
  bool storeBwBuffer() {
    calls.push_back("save");
    if (failure == 1) return false;
    saved = frame; return true;
  }
  void restoreBwBuffer() {
    calls.push_back("restore"); assert(!saved.empty()); frame = saved; saved.clear();
  }
  bool captureGrayscaleBaseBuffer() const {
    calls.push_back("base");
    if (failure == 2) return false;
    base = frame; return true;
  }
  void copyGrayscaleLsbBuffers() const { calls.push_back("lsb"); if (failure != 3) lsb = frame; }
  void copyGrayscaleMsbBuffers() const { calls.push_back("msb"); if (failure != 4) msb = frame; }
  bool grayscaleBuffersReady() const { return !base.empty() && !lsb.empty() && !msb.empty(); }
  void displayGrayBuffer(HalDisplay::RefreshMode mode) const {
    assert(mode == HalDisplay::HALF_REFRESH && grayscaleBuffersReady());
    assert(saved.empty() && frame == base); // Restore before publishing.
    calls.push_back("gray"); ++presentations; ++grayPresentations;
    shown.resize(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
      // Exact HalDisplay::grayscaleValueForBit truth table: white base wins;
      // LSB-only=dark gray, MSB-only=light gray, both=dark gray, neither=black.
      shown[static_cast<size_t>(y) * width + x] = bit(base, x, y) ? 3 : bit(lsb, x, y) ? 1 : bit(msb, x, y) ? 2 : 0;
    }
  }
  void displayBuffer() const {
    calls.push_back("bw"); ++presentations;
    shown.resize(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
      shown[static_cast<size_t>(y) * width + x] = bit(frame, x, y) ? 3 : 0;
    }
  }
  uint8_t at(int x, int y) const { return shown.at(static_cast<size_t>(y) * width + x); }
};
struct BaseTheme {
  static int getLineHeightForRole(const GfxRenderer&, int, TextRole) { return state.contentHeight; }
  static int getTextWidthForRole(const GfxRenderer& r, int f, TextRole, const char* s) { return r.getTextWidth(f, s); }
  static std::string truncatedTextForRole(const GfxRenderer& r, int f, TextRole, const char* s, int w) {
    return r.truncatedText(f, s, w);
  }
  static std::vector<std::string> wrappedTextForRole(const GfxRenderer&, int, TextRole, const char* s, int w, int n) {
    std::vector<std::string> lines;
    std::string remaining(s);
    const size_t count = static_cast<size_t>(std::max(0, w / 6));
    while (n-- > 0 && !remaining.empty() && count) { lines.push_back(remaining.substr(0, count)); remaining.erase(0, count); }
    return lines;
  }
  static void drawTextForRole(const GfxRenderer& r, int f, TextRole, int x, int y, const char* s, bool black) {
    r.drawText(f, x, y, s, black);
  }
  void drawRecentBookCover(GfxRenderer&, Rect, const std::vector<RecentBook>&, int, bool&, bool&, bool&,
                           std::function<bool()>) const { ++state.delegates; }
};
struct UITheme {
  ThemeMetrics metrics;
  BaseTheme theme;
  static UITheme& getInstance() { static UITheme instance; return instance; }
  const ThemeMetrics& getMetrics() const { return metrics; }
  const BaseTheme& getTheme() const { return theme; }
  static std::string getCoverThumbPath(const std::string& path, int) { return path; }
};
#define GUI UITheme::getInstance().getTheme()
#define LOG_ERR(...) ((void)0)
constexpr int UI_12_FONT_ID = 12, UI_10_FONT_ID = 10;
#include "I18n.h"
inline I18n& I18n::getInstance() { static I18n instance; return instance; }
inline const char* I18n::get(StrId id) const {
  return id == StrId::STR_CONTINUE_READING ? "Continue reading" : "No open book";
}
