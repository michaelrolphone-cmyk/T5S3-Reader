#include "components/ReadingCardStyle.h"

#include <cassert>
#include <climits>
#include <iostream>
#include <vector>

using namespace ReadingCardStyle;

static bool contains(const Box& a, const Box& b) {
  return b.x >= a.x && b.y >= a.y && b.x + b.width <= a.x + a.width && b.y + b.height <= a.y + a.height;
}

int main() {
  int layouts = 0;
  for (int width : {1, 8, 32, 96, 160, 240, 320, 480, 540, 960}) {
    for (int height : {1, 8, 32, 64, 160, 350, 400}) {
      for (auto image : {Box{0, 0, 0, 0}, Box{0, 0, 240, 360}, Box{0, 0, 96, 128}, Box{0, 0, 1000, 50},
                         Box{0, 0, 1, INT_MAX}, Box{0, 0, INT_MAX, 1}}) {
        Box bounds{13, 41, width, height};
        auto l = layout(bounds, 20, 300, image.width, image.height);
        assert(contains(bounds, l.card));
        assert(contains(l.card, l.text));
        assert(l.radius >= 0 && l.radius <= l.card.width / 2 && l.radius <= l.card.height / 2);
        if (l.cover.width > 0) {
          assert(contains(l.card, l.cover));
          assert(l.cover.x + l.cover.width + 20 <= l.text.x);
          assert(l.cover.height <= 300);
          assert(l.cover.width <= image.width && l.cover.height <= image.height);
          assert(l.text.width > 0);
          const int64_t error = std::abs(static_cast<int64_t>(l.cover.width) * image.height -
                                          static_cast<int64_t>(l.cover.height) * image.width);
          assert(error <= std::max(image.width, image.height));
        }
        ++layouts;
      }
      const Box card{0, 0, width, height};
      std::vector<int> raster(width * height, -1);
      paint(card, 28, [&](int x, int y, int light) {
        assert(x >= 0 && x < width && y >= 0 && y < height);
        assert(light >= 0 && light <= 16);
        assert(raster[y * width + x] == -1);
        raster[y * width + x] = light;
      });
      assert(raster[(height / 2) * width + width / 2] >= 0);
      if (width >= 64 && height >= 64) {
        assert(raster[0] == -1);
        assert(raster[width * height - 1] == -1);
        assert(raster[width / 2] == 0);  // Outer black keyline.
        assert(raster[width + 40] > raster[width + width - 41]);
        assert(raster[(height - 2) * width + width - 41] > raster[(height - 2) * width + 40]);
        assert(raster[(height / 2) * width + width / 2] <= 2);
      }
    }
  }
  for (int light = 0; light <= 16; ++light) {
    int white = 0;
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) white += !blackPixel(x, y, light);
    assert(white == light);
  }
  int emitted = 0;
  paint(Box{0, 0, -1, 300}, 28, [&](int, int, int) { ++emitted; });
  paint(Box{0, 0, 500, 0}, 28, [&](int, int, int) { ++emitted; });
  assert(emitted == 0);
  assert(layout(Box{0, 0, -1, 300}, 20, 300, 1, 1).card.width == 0);
  std::cout << "PASS: " << layouts << " layouts, raster bounds, opposed highlights and all 17 dither levels\n";
}
