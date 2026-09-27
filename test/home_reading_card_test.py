#!/usr/bin/env python3
"""Compile the production card against a recording renderer; no board/emulator claim."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

API = r'''
#pragma once
#include <algorithm>
#include <cassert>
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
class GfxRenderer {
 public:
  struct Text { int x, y, font; std::string value; bool black; };
  int width = 540, height = 960;
  mutable int pixels = 0, bitmaps = 0, focus = 0, coverBases = 0;
  mutable std::vector<Text> texts;
  mutable Rect bitmapRect{}, coverBase{};
  int getScreenWidth() const { return width; }
  int getScreenHeight() const { return height; }
  void drawPixel(int x, int y, bool) const { assert(x >= 0 && y >= 0 && x < width && y < height); ++pixels; }
  void fillRect(int x, int y, int w, int h, bool black) const {
    assert(!black && w > 0 && h > 0 && x >= 0 && y >= 0 && x + w <= width && y + h <= height);
    coverBase = {x, y, w, h}; ++coverBases;
  }
  void drawBitmap(const Bitmap&, int x, int y, int w, int h) const {
    // Production BMP drawing skips white pixels, so the card must first supply
    // a white paper base; neither the 1-bit nor 2-bit renderer upscales art.
    assert(coverBases > 0 && coverBase.x == x && coverBase.y == y && coverBase.width == w && coverBase.height == h);
    assert(w <= state.width && h <= state.height);
    assert(w > 0 && h > 0 && x >= 0 && y >= 0 && x + w <= width && y + h <= height);
    bitmapRect = {x, y, w, h}; ++bitmaps;
  }
  void drawRect(int x, int y, int w, int h, bool black) const {
    assert(!black && w > 0 && h > 0 && x >= 0 && y >= 0 && x + w <= width && y + h <= height);
  }
  void drawRoundedRect(int x, int y, int w, int h, int, int radius, bool black) const {
    assert(!black && radius >= 0 && w > 0 && h > 0 && x >= 0 && y >= 0 && x + w <= width && y + h <= height); ++focus;
  }
  int getTextWidth(int, const char* text) const { return static_cast<int>(std::strlen(text)) * 6; }
  int getLineHeight(int) const { return 20; }
  std::string truncatedText(int, const char* text, int width) const {
    return std::string(text).substr(0, static_cast<size_t>(std::max(0, width / 6)));
  }
  void drawText(int font, int x, int y, const char* text, bool black) const {
    assert(!black && x >= 0 && y >= 0 && x + getTextWidth(font, text) <= width && y + 20 <= height);
    texts.push_back({x, y, font, text, black});
  }
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
constexpr int UI_12_FONT_ID = 12, UI_10_FONT_ID = 10;
constexpr int STR_CONTINUE_READING = 1, STR_NO_OPEN_BOOK = 2;
inline const char* tr(int value) { return value == 1 ? "Continue reading" : "No open book"; }
'''

HARNESS = r'''
#include "TestApi.h"
#include "components/HomeReadingCard.h"
#include <climits>
#include <iostream>
int main() {
  const Rect slot{0, 55, 540, 350};
  const std::vector<RecentBook> books{{"/Books/example.epub", "A real book title", "An Author", "cover.bmp"}};
  auto reset = [] { state = TestState{}; UITheme::getInstance().metrics = ThemeMetrics{}; };
  reset();
  {
    GfxRenderer r;
    bool painted = false, cached = false, restored = false;
    int stores = 0;
    auto store = [&] { assert(r.focus == 0); ++stores; return true; };
    HomeReadingCard::draw(r, slot, books, 0, painted, cached, restored, store);
    assert(painted && cached && stores == 1 && r.focus == 1 && r.bitmaps == 1 && state.opens == 1 && state.closes == 1);
    assert(r.texts.size() >= 3);
    for (const auto& t : r.texts) {
      assert(!t.black && t.x > r.bitmapRect.x + r.bitmapRect.width);
      assert(t.y >= slot.y && t.y + 20 <= slot.y + slot.height);
    }
    const int writes = r.pixels;
    r.focus = 0; restored = true;
    HomeReadingCard::draw(r, slot, books, 1, painted, cached, restored, store);
    assert(r.pixels == writes && stores == 1 && r.focus == 0 && state.opens == 1);
    // Thumbnail generation invalidates the card even when an old buffer was restored.
    painted = false;
    HomeReadingCard::draw(r, slot, books, 1, painted, cached, restored, store);
    assert(r.pixels > writes && stores == 2 && state.opens == 2);
  }
  for (int failure = 0; failure < 4; ++failure) {
    reset();
    GfxRenderer r;
    bool painted = false, cached = false, restored = false;
    auto fallback = books;
    if (failure == 0) state.open = false;
    if (failure == 1) state.parse = false;
    if (failure == 2) state.width = 0;
    if (failure == 3) fallback[0].coverBmpPath.clear();
    HomeReadingCard::draw(r, slot, fallback, 1, painted, cached, restored, [] { return true; });
    assert(r.bitmaps == 0 && r.coverBases == 0 && r.texts.size() >= 3 && painted && cached);
    for (const auto& t : r.texts) assert(!t.black && t.x == 44);
  }
  reset();
  {
    GfxRenderer r;
    bool painted = false, cached = false, restored = false;
    HomeReadingCard::draw(r, slot, {}, 0, painted, cached, restored, [] { return true; });
    assert(r.bitmaps == 0 && r.focus == 0 && state.opens == 0 && r.texts.size() == 1);
    assert(r.texts[0].value == "No open book" && !r.texts[0].black);
  }
  reset();
  {
    GfxRenderer r;
    bool painted = false, cached = false, restored = false;
    HomeReadingCard::draw(r, slot, books, 1, painted, cached, restored, [] { return false; });
    assert(!painted && !cached);
    int writes = r.pixels;
    HomeReadingCard::draw(r, slot, books, 1, painted, cached, restored, {});
    assert(!painted && !cached && r.pixels > writes && state.opens == 2);
  }
  reset();
  {
    GfxRenderer r;
    bool painted = false, cached = false, restored = false;
    UITheme::getInstance().metrics.homeContinueReadingInMenu = true;
    HomeReadingCard::draw(r, slot, books, 0, painted, cached, restored, [] { return true; });
    assert(r.focus == 0 && r.bitmaps == 1);
    UITheme::getInstance().metrics.homeRecentBooksCount = 3;
    int writes = r.pixels;
    HomeReadingCard::draw(r, slot, books, 0, painted, cached, restored, [] { return true; });
    assert(state.delegates == 1 && r.pixels == writes);
  }
  reset();
  for (const Rect box : {Rect{0, 0, 1, 1}, Rect{-20, -20, 50, 50}, Rect{0, 0, -5, 50},
                         Rect{INT_MAX, INT_MAX, 400, 400}, Rect{530, 950, INT_MAX, INT_MAX},
                         Rect{0, 55, 160, 100}, Rect{0, 55, 540, 80}}) {
    GfxRenderer r;
    bool painted = false, cached = false, restored = false;
    HomeReadingCard::draw(r, box, books, 0, painted, cached, restored, [] { return true; });
  }
  reset();
  {
    GfxRenderer r;
    bool painted = false, cached = false, restored = false;
    auto titleOnly = books;
    titleOnly[0].title.clear(); titleOnly[0].author.clear(); titleOnly[0].coverBmpPath.clear();
    HomeReadingCard::draw(r, slot, titleOnly, 1, painted, cached, restored, [] { return true; });
    assert(r.texts.back().value == "example.epub");
  }
  reset();
  {
    GfxRenderer r;
    bool painted = false, cached = false, restored = false;
    HomeReadingCard::draw(r, Rect{0, 55, 540, 80}, books, 1, painted, cached, restored, [] { return true; });
    assert(r.texts.size() == 1 && r.texts[0].value == "A real book title");
  }
  reset();
  {
    state.contentHeight = 100;
    GfxRenderer r;
    bool painted = false, cached = false, restored = false;
    auto longTitle = books;
    longTitle[0].title = std::string(500, 'W');
    HomeReadingCard::draw(r, slot, longTitle, 1, painted, cached, restored, [] { return true; });
    assert(!r.texts.empty());
    for (const auto& text : r.texts) {
      assert(!text.black);
      assert(text.y >= slot.y && text.y + (text.value == "Continue reading" ? 20 : 100) <= slot.y + slot.height);
    }
  }
  reset();
  {
    state.width = 96; state.height = 128;
    GfxRenderer r;
    bool painted = false, cached = false, restored = false;
    HomeReadingCard::draw(r, slot, books, 1, painted, cached, restored, [] { return true; });
    assert(r.bitmaps == 1 && r.coverBases == 1 && r.bitmapRect.width == 96 && r.bitmapRect.height == 128);
  }
  std::cout << "PASS: production card rendering, white metadata, cache/focus isolation, failure fallback, theme routing and clipping\n";
}
'''


class ReadingCardTests(unittest.TestCase):
    def test_production_geometry_and_renderer(self):
        with tempfile.TemporaryDirectory(prefix="riscrte-reading-card-") as temp:
            work = Path(temp)
            (work / "TestApi.h").write_text(API)
            for name in ("Bitmap.h", "GfxRenderer.h", "HalStorage.h", "I18n.h", "RecentBooksStore.h",
                         "fontIds.h", "components/UITheme.h", "components/themes/BaseTheme.h"):
                path = work / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('#include "TestApi.h"\n')
            (work / "harness.cpp").write_text(HARNESS)
            flags = [os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                     "-fsanitize=undefined", "-fno-sanitize-recover=all", "-I" + str(work), "-I" + str(ROOT / "src")]
            for name, sources in (
                ("style", [ROOT / "test/reading_card_style_test.cpp"]),
                ("card", [ROOT / "src/components/HomeReadingCard.cpp", work / "harness.cpp"]),
            ):
                executable = work / name
                subprocess.run(flags + [str(p) for p in sources] + ["-o", str(executable)], check=True)
                subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
