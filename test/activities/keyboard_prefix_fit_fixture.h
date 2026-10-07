#pragma once
#include <EpdFontFamily.h>
#include <SdCardFont.h>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

struct Cost {
  uint64_t measures = 0, measuredBytes = 0, prefixes = 0, glyphs = 0, kerns = 0, ligatures = 0;
};
inline Cost cost;
inline uint32_t clockNow = 0, clockStep = 0;
inline unsigned yields = 0;
inline uint32_t testMillis() { uint32_t now = clockNow; clockNow += clockStep; return now; }
inline void testDelay(unsigned ticks) { assert(ticks == 1); ++yields; }
inline std::ostringstream records;
template <typename... T> void record(const char* operation, const T&... values) {
  records << operation;
  ((records << '|' << values), ...);
  records << '\n';
}
class GfxRenderer {
 public:
  int width = 400, height = 800;
  std::map<int, EpdFontFamily> fontMap;
  std::map<int, SdCardFont*> sdCardFonts_;
  bool isSdCardFont(int id) const { return sdCardFonts_.count(id) != 0; }
  int getTextAdvanceX(int, const char*, EpdFontFamily::Style = EpdFontFamily::REGULAR) const;
  int getTextWidth(int, const char*, EpdFontFamily::Style = EpdFontFamily::REGULAR) const;
  int getLineHeight(int) const;
  bool getTextFittingPrefix(int, const char*, size_t, int, size_t&,
                           EpdFontFamily::Style = EpdFontFamily::REGULAR) const;
  int getScreenWidth() const { return width; }
  int getScreenHeight() const { return height; }
  void clearScreen() { record("clear"); }
  void displayBuffer() { record("present"); }
  void drawText(int id, int x, int y, const char* text, bool black = true) const {
    record("text", id, x, y, black, std::quoted(text));
  }
  void drawCenteredText(int id, int y, const char* text, bool black = true) const {
    record("centered", id, y, black, std::quoted(text));
  }
  void fillRect(int x, int y, int w, int h, bool black) const { record("fill", x, y, w, h, black); }
  void drawLine(int x1, int y1, int x2, int y2, int w, bool black) const {
    record("line", x1, y1, x2, y2, w, black);
  }
};
