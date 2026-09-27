#pragma once

#include <algorithm>
#include <cstdint>

// Home-only chrome. Ordered monochrome shading keeps the existing fast refresh
// path; no grayscale planes, panel APIs, heap buffers or animation are needed.
namespace ReadingCardStyle {
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

inline Layout layout(Box bounds, int sidePadding, int coverHeight, int imageWidth, int imageHeight) {
  Layout result;
  if (bounds.width <= 0 || bounds.height <= 0) return result;
  const int margin = std::clamp(sidePadding, 0, bounds.width / 4);
  result.card = {bounds.x + margin, bounds.y, bounds.width - margin * 2, bounds.height};
  result.radius = std::min({28, result.card.width / 4, result.card.height / 4});
  const int inset = std::min({24, result.card.width / 4, result.card.height / 4});
  result.text = {result.card.x + inset, result.card.y + inset,
                 result.card.width - inset * 2, result.card.height - inset * 2};

  // Reserve readable text first. Small viewports degrade to a text-only card,
  // rather than stretching a cover or letting it overlap the title.
  if (imageWidth <= 0 || imageHeight <= 0 || coverHeight <= 0 || result.text.width < 240 ||
      result.text.height < 64) return result;
  // GfxRenderer downsizes BMPs but does not upscale them. Keep the frame and
  // metadata aligned to the pixels that the renderer will actually produce.
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
  const int gap = 20;
  result.cover = {result.text.x, result.card.y + (result.card.height - height) / 2, width, height};
  result.text.x += width + gap;
  result.text.width -= width + gap;
  return result;
}

// Inset of a rounded scanline. Invalid rows denote the empty inner shape.
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

// Whiteness in sixteenths: two opposed specular highlights that taper along
// both the straight edges and the curved corners, not a uniform white border.
inline int rimLight(int x, int y, int width, int height) {
  const int nx = x * 256 / std::max(1, width - 1);
  const int ny = y * 256 / std::max(1, height - 1);
  const int distance = std::min(nx + ny, 512 - nx - ny);
  return 2 + std::max(0, 256 - distance) * 14 / 256;
}

inline bool blackPixel(int x, int y, int whiteLevel) {
  static constexpr uint8_t bayer[4][4] = {
      {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  return bayer[static_cast<unsigned>(y) & 3u][static_cast<unsigned>(x) & 3u] >= whiteLevel;
}

// The emitter receives local coordinates and a 0..16 whiteness. This is also
// used by host raster tests, so the tested geometry is the production geometry.
template <typename Emit>
void paint(const Box& card, int radius, Emit emit) {
  if (card.width <= 0 || card.height <= 0) return;
  radius = std::clamp(radius, 0, std::min(card.width, card.height) / 2);
  for (int y = 0; y < card.height; ++y) {
    const int outer = rowInset(card.width, card.height, radius, y);
    const int rim = 1 + rowInset(card.width - 2, card.height - 2, std::max(0, radius - 1), y - 1);
    const int face = 3 + rowInset(card.width - 6, card.height - 6, std::max(0, radius - 3), y - 3);
    const int bodyLight = y < card.height / 4 ? 2 : 1;
    for (int x = outer; x < card.width - outer; ++x) {
      int light = 0;  // Dark outer keyline and recessed edge.
      if (y >= 3 && y < card.height - 3 && x >= face && x < card.width - face) {
        light = bodyLight;
      } else if (y >= 1 && y < card.height - 1 && x >= rim && x < card.width - rim) {
        light = rimLight(x, y, card.width, card.height);
      }
      emit(x, y, light);
    }
  }
}
}  // namespace ReadingCardStyle
