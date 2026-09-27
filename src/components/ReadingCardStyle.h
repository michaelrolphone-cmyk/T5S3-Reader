#pragma once

#include <algorithm>
#include <cstdint>

// Home-only chrome in real display tones, not spatial black/white dithering.
namespace ReadingCardStyle {
enum Tone : uint8_t { Black = 0, DarkGray = 1, LightGray = 2, White = 3 };
struct Box {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};
struct Layout {
  Box card;
  Box cover;
  Box text;
  int radius = 0;
};

inline Box clip(Box box, int screenWidth, int screenHeight) {
  if (screenWidth <= 0 || screenHeight <= 0 || box.width <= 0 || box.height <= 0) return {};
  const int left = std::clamp(box.x, 0, screenWidth);
  const int top = std::clamp(box.y, 0, screenHeight);
  const int right = static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(box.x) + box.width, 0, screenWidth));
  const int bottom = static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(box.y) + box.height, 0, screenHeight));
  if (right <= left || bottom <= top) return {};
  return {left, top, right - left, bottom - top};
}

inline Layout layout(Box bounds, int sidePadding, int coverHeight, int imageWidth, int imageHeight) {
  Layout result;
  if (bounds.width <= 0 || bounds.height <= 0) return result;
  const int margin = std::clamp(sidePadding, 0, bounds.width / 4);
  result.card = {bounds.x + margin, bounds.y, bounds.width - margin * 2, bounds.height};
  result.radius = std::min({36, result.card.width / 4, result.card.height / 4});
  const int inset = std::min({24, result.card.width / 4, result.card.height / 4});
  result.text = {result.card.x + inset, result.card.y + inset,
                 result.card.width - inset * 2, result.card.height - inset * 2};
  if (imageWidth <= 0 || imageHeight <= 0 || coverHeight <= 0 || result.text.width < 240 ||
      result.text.height < 64) return result;
  // GfxRenderer only downsizes BMPs. Never invent an upscaled art rectangle.
  const int maxWidth = std::min(result.text.width * 2 / 5, imageWidth);
  const int maxHeight = std::min({coverHeight, result.text.height, imageHeight});
  int width = maxWidth;
  int height = 0;
  if (static_cast<int64_t>(imageHeight) * width > static_cast<int64_t>(maxHeight) * imageWidth) {
    height = maxHeight;
    width = static_cast<int>(static_cast<int64_t>(imageWidth) * height / imageHeight);
  } else {
    height = static_cast<int>(static_cast<int64_t>(imageHeight) * width / imageWidth);
  }
  if (width <= 0 || height <= 0) return result;
  result.cover = {result.text.x, result.card.y + (result.card.height - height) / 2, width, height};
  result.text.x += width + 20;
  result.text.width -= width + 20;
  return result;
}

inline int rowInset(int width, int height, int radius, int row) {
  if (width <= 0 || height <= 0 || row < 0 || row >= height) return width;
  radius = std::clamp(radius, 0, std::min(width, height) / 2);
  const int edge = std::min(row, height - 1 - row);
  if (edge >= radius) return 0;
  const int dy = radius - 1 - edge;
  const int rr = radius - 1;
  int dx = rr;
  while (dx > 0 && dx * dx + dy * dy > rr * rr) --dx;
  return rr - dx;
}

inline bool inside(int x, int y, int width, int height, int radius, int inset) {
  const int w = width - inset * 2;
  const int h = height - inset * 2;
  const int row = y - inset;
  if (w <= 0 || h <= 0 || row < 0 || row >= h) return false;
  const int edge = inset + rowInset(w, h, std::max(0, radius - inset), row);
  return x >= edge && x < width - edge;
}

// Both corners catch the light, but the connecting rim steps down through
// light gray and dark gray. Only the strongest glints reach pure white.
inline Tone rimTone(int x, int y, int width, int height) {
  const int nx = static_cast<int>(static_cast<int64_t>(x) * 256 / std::max(1, width - 1));
  const int ny = static_cast<int>(static_cast<int64_t>(y) * 256 / std::max(1, height - 1));
  const int distance = std::min(nx + ny, 512 - nx - ny);
  return distance < 80 ? White : distance < 176 ? LightGray : DarkGray;
}

// A solid black face, recessed dark edge, two-pixel directional rim, and a
// narrow curved reflection inside the top lip. All gray pixels are outside
// the 24px content inset, so replay cannot recolor black cover-art pixels.
inline Tone toneAt(int x, int y, int width, int height, int radius) {
  if (!inside(x, y, width, height, radius, 1)) return Black;
  if (!inside(x, y, width, height, radius, 3)) return rimTone(x, y, width, height);
  if (!inside(x, y, width, height, radius, 5)) return Black;
  if (width >= 128 && height >= 96) {
    // A shallow ellipse keeps the reflection's lower edge curved rather than
    // introducing the old flat, quarter-height brightness step.
    const int dx = x - width / 2;
    const int half = std::max(1, width / 2 - 12);
    const int arch = 10 - static_cast<int>(static_cast<int64_t>(dx) * dx * 10 /
                                         (static_cast<int64_t>(half) * half));
    if (y >= 7 && y < 7 + arch && inside(x, y, width, height, radius, 7)) return DarkGray;
    if (y >= height - 9 && y < height - 7 && x > width / 2 && x < width - radius) return DarkGray;
  }
  return Black;
}

// These are component bits, NOT ordinary black-pixel draw booleans. Existing
// HalDisplay combines black base + LSB=1 as dark gray, MSB=1 as light gray.
inline bool componentBit(Tone tone, bool lsb) {
  return lsb ? tone == DarkGray : tone == LightGray;
}

// Cache each inset arc once per scanline, not once per pixel. This keeps both
// gray passes bounded without per-pixel square-root/arc walks on the ESP32.
inline int insetEdge(int width, int height, int radius, int row, int inset) {
  if (width <= 2 * inset || height <= 2 * inset || row < inset || row >= height - inset) return width;
  return inset + rowInset(width - 2 * inset, height - 2 * inset, std::max(0, radius - inset), row - inset);
}

template <typename Emit>
void paint(const Box& card, int radius, Emit emit) {
  if (card.width <= 0 || card.height <= 0) return;
  const int width = card.width, height = card.height;
  radius = std::clamp(radius, 0, std::min(width, height) / 2);
  for (int y = 0; y < height; ++y) {
    const int edge = rowInset(width, height, radius, y);
    const int rim = insetEdge(width, height, radius, y, 1);
    const int recess = insetEdge(width, height, radius, y, 3);
    const int face = insetEdge(width, height, radius, y, 5);
    const int reflection = insetEdge(width, height, radius, y, 7);
    for (int x = edge; x < width - edge; ++x) {
      Tone tone = Black;
      if (x >= rim && x < width - rim) {
        if (x < recess || x >= width - recess) {
          tone = rimTone(x, y, width, height);
        } else if (x >= face && x < width - face && width >= 128 && height >= 96) {
          if (y >= 7 && y < 17 && x >= reflection && x < width - reflection) {
            const int dx = x - width / 2;
            const int half = width / 2 - 12;
            const int arch = 10 - static_cast<int>(static_cast<int64_t>(dx) * dx * 10 /
                                                 (static_cast<int64_t>(half) * half));
            if (y < 7 + arch) tone = DarkGray;
          }
          if (y >= height - 9 && y < height - 7 && x > width / 2 && x < width - radius) tone = DarkGray;
        }
      }
      emit(x, y, tone);
    }
  }
}
}  // namespace ReadingCardStyle
