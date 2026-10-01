#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include "DeskClockDigits.h"

// Pure, bounded drawing shared by the retained clock and the desktop preview.
// All assets live in flash: minute wakes must not mount SD or load reader fonts.
namespace DeskClockFaces {
enum Face : uint8_t { Segments, Sans, Serif, Minimal, Railway, Deco, Count };
inline Face sanitize(uint8_t face) { return face < Count ? static_cast<Face>(face) : Segments; }
constexpr float pi = 3.14159265358979323846f;

struct Point { int x, y; };
inline Point point(int cx, int cy, int radius, float minutes) {
  const float angle = minutes * pi / 30;
  return {cx + static_cast<int>(std::lround(std::sin(angle) * radius)),
          cy - static_cast<int>(std::lround(std::cos(angle) * radius))};
}

template<class Canvas>
void disk(Canvas& c, int x, int y, int radius, bool black = true) {
  for (int dy = -radius; dy <= radius; ++dy) {
    const int dx = static_cast<int>(std::sqrt(static_cast<float>(radius * radius - dy * dy)));
    c.fillRect(x - dx, y + dy, 2 * dx + 1, 1, black);
  }
}

// GfxRenderer's thick line offsets only in Y, making vertical hands thin.
// A small circular brush gives these dials equal stroke weight at every angle.
template<class Canvas>
void stroke(Canvas& c, int x, int y, int xx, int yy, int width) {
  const int dx = std::abs(xx - x), dy = -std::abs(yy - y);
  const int sx = x < xx ? 1 : -1, sy = y < yy ? 1 : -1;
  int error = dx + dy;
  for (;;) {
    disk(c, x, y, width / 2);
    if (x == xx && y == yy) break;
    const int twice = 2 * error;
    if (twice >= dy) { error += dy; x += sx; }
    if (twice <= dx) { error += dx; y += sy; }
  }
}

template<class Canvas>
void ring(Canvas& c, int x, int y, int radius, int thickness) {
  // Integer scanlines avoid gaps and do not erase anything inside the ring.
  for (int dy = -radius; dy <= radius; ++dy) {
    const int outer = static_cast<int>(std::sqrt(static_cast<float>(radius * radius - dy * dy)));
    const int innerRadius = radius - thickness;
    if (std::abs(dy) >= innerRadius) c.fillRect(x - outer, y + dy, 2 * outer + 1, 1);
    else {
      const int inner = static_cast<int>(std::sqrt(static_cast<float>(innerRadius * innerRadius - dy * dy)));
      c.fillRect(x - outer, y + dy, outer - inner, 1);
      c.fillRect(x + inner + 1, y + dy, outer - inner, 1);
    }
  }
}

template<class Canvas>
void digit(Canvas& c, int value, int x, int y, int unit) {
  static constexpr uint8_t masks[] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
  const uint8_t mask = value < 0 ? 0x40 : masks[value];
  const int w = 6 * unit, h = 10 * unit, gap = std::max(2, unit / 8);
  if (mask & 1) c.fillRect(x + unit, y, w - 2 * unit, unit - gap);
  if (mask & 2) c.fillRect(x + w - unit, y + unit, unit - gap, h / 2 - unit - gap);
  if (mask & 4) c.fillRect(x + w - unit, y + h / 2 + gap, unit - gap, h / 2 - unit - gap);
  if (mask & 8) c.fillRect(x + unit, y + h - unit, w - 2 * unit, unit - gap);
  if (mask & 16) c.fillRect(x, y + h / 2 + gap, unit - gap, h / 2 - unit - gap);
  if (mask & 32) c.fillRect(x, y + unit, unit - gap, h / 2 - unit - gap);
  if (mask & 64) c.fillRect(x + unit, y + h / 2 - unit / 2, w - 2 * unit, unit - gap);
}

// Rasterized at clock size, rather than magnifying a small UI font. Scanline
// rectangles use the same integer boundaries so scaling cannot open row gaps.
template<class Canvas>
void numeral(Canvas& c, int value, int x, int y, int height, bool serif) {
  using namespace DeskClockDigits;
  const auto& glyph = (serif ? NotoSerifGlyphs : NotoSansGlyphs)[value];
  const Span* spans = serif ? NotoSerifSpans : NotoSansSpans;
  for (unsigned i = glyph.start; i < glyph.start + glyph.count; ++i) {
    const auto& s = spans[i];
    const int left = s.x * height / DeskClockDigits::height;
    const int top = s.y * height / DeskClockDigits::height;
    const int w = (s.x + s.width) * height / DeskClockDigits::height - left;
    const int h = (s.y + 1) * height / DeskClockDigits::height - top;
    if (w && h) c.fillRect(x + left, y + top, w, h);
  }
}

inline int numeralWidth(int value, int height, bool serif) {
  return (serif ? DeskClockDigits::NotoSerifGlyphs : DeskClockDigits::NotoSansGlyphs)[value].width *
         height / DeskClockDigits::height;
}

template<class Canvas>
void number(Canvas& c, int value, int cx, int cy, int height, bool serif) {
  const int w = numeralWidth(value % 10, height, serif) +
                (value >= 10 ? numeralWidth(value / 10, height, serif) : 0);
  int x = cx - w / 2;
  if (value >= 10) {
    numeral(c, value / 10, x, cy - height / 2, height, serif);
    x += numeralWidth(value / 10, height, serif);
  }
  numeral(c, value % 10, x, cy - height / 2, height, serif);
}

template<class Canvas>
void draw(Canvas& c, uint8_t selection, int hour24, int minute, bool use12Hour, bool valid) {
  const Face face = sanitize(selection);
  const int width = c.getScreenWidth(), height = c.getScreenHeight();
  const int hour = use12Hour ? (hour24 % 12 == 0 ? 12 : hour24 % 12) : hour24;
  if (face == Segments || !valid) {
    const int unit = std::max(4, std::min((width - 96) / 29, (height - 200) / 10));
    const int left = (width - 29 * unit) / 2, top = (height - 10 * unit) / 2;
    const int values[] = {hour / 10, hour % 10, minute / 10, minute % 10};
    const int positions[] = {0, 7, 16, 23};
    for (int i = 0; i < 4; ++i) {
      if (i == 0 && valid && use12Hour && hour < 10) continue;
      digit(c, valid ? values[i] : -1, left + positions[i] * unit, top, unit);
    }
    c.fillRect(left + 14 * unit, top + 3 * unit, unit, unit);
    c.fillRect(left + 14 * unit, top + 6 * unit, unit, unit);
    return;
  }
  if (face == Sans || face == Serif) {
    const bool serif = face == Serif;
    const int values[] = {hour / 10, hour % 10, 10, minute / 10, minute % 10};
    const int first = use12Hour && hour < 10 ? 1 : 0;
    int naturalWidth = 0;
    for (int i = first; i < 5; ++i) naturalWidth += numeralWidth(values[i], 224, serif) + 12;
    naturalWidth -= 12;
    const int size = std::min(224, std::min(height - 200, (width - 96) * 224 / naturalWidth));
    const int gap = 12 * size / 224;
    int total = -gap;
    for (int i = first; i < 5; ++i) total += numeralWidth(values[i], size, serif) + gap;
    int x = (width - total) / 2;
    for (int i = first; i < 5; ++i) {
      numeral(c, values[i], x, (height - size) / 2, size, serif);
      x += numeralWidth(values[i], size, serif) + gap;
    }
    return;
  }

  const int cx = width / 2, cy = height / 2;
  const int r = std::min((height - 180) / 2, (width - 96) / 2);
  if (face != Minimal) ring(c, cx, cy, r, face == Railway ? 3 : 2);
  if (face == Deco) ring(c, cx, cy, r - 7, 1);
  for (int tick = 0; tick < 60; ++tick) {
    const bool major = tick % 5 == 0;
    if (face == Minimal && !major) continue;
    if (face == Deco && tick % 15 == 0) continue;
    const int outer = r - (face == Minimal ? 0 : 13);
    const int length = major ? (face == Railway ? 20 : 12) : 4;
    const Point a = point(cx, cy, outer, tick), b = point(cx, cy, outer - length, tick);
    stroke(c, a.x, a.y, b.x, b.y, major ? (face == Railway ? 5 : 2) : 1);
  }
  if (face == Deco) {
    for (int n = 3; n <= 12; n += 3) {
      const Point p = point(cx, cy, r - 30, n * 5);
      number(c, n, p.x, p.y, 29, true);
    }
  }
  const float hours = (hour24 % 12) * 5 + minute / 12.0f;
  const Point h = point(cx, cy, r * 50 / 100, hours);
  const Point m = point(cx, cy, r * 76 / 100, minute);
  const int hourWidth = face == Railway ? 9 : (face == Deco ? 6 : 5);
  stroke(c, cx, cy, h.x, h.y, hourWidth);
  stroke(c, cx, cy, m.x, m.y, face == Railway ? 5 : 3);
  if (face == Deco) {
    const Point tail = point(cx, cy, r / 7, minute + 30);
    stroke(c, cx, cy, tail.x, tail.y, 3);
    disk(c, tail.x, tail.y, 4);
  }
  disk(c, cx, cy, face == Railway ? 8 : 6);
  if (face == Minimal || face == Deco) disk(c, cx, cy, 2, false);
}
}  // namespace DeskClockFaces
