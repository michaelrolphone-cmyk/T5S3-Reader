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
        assert(contains(bounds, l.card) && contains(l.card, l.text));
        if (l.cover.width > 0) {
          assert(contains(l.card, l.cover) && l.cover.x + l.cover.width + 20 <= l.text.x);
          assert(l.cover.width <= image.width && l.cover.height <= image.height && l.cover.height <= 300);
          const int64_t error = std::abs(static_cast<int64_t>(l.cover.width) * image.height -
                                          static_cast<int64_t>(l.cover.height) * image.width);
          assert(error <= std::max(image.width, image.height));
        }
        ++layouts;
      }
      const Box card{0, 0, width, height};
      std::vector<int> raster(width * height, -1);
      paint(card, 36, [&](int x, int y, Tone tone) {
        assert(x >= 0 && x < width && y >= 0 && y < height);
        assert(tone >= Black && tone <= White && raster[y * width + x] == -1);
        assert(tone == toneAt(x, y, width, height, std::min({36, width / 2, height / 2})));
        raster[y * width + x] = tone;
        if (width >= 128 && height >= 96 && x >= 24 && x < width - 24 && y >= 24 && y < height - 24) {
          assert(tone == Black); // Gray replay never touches metadata/art.
        }
      });
      assert(raster[(height / 2) * width + width / 2] >= 0);
      if (width >= 160 && height >= 160) {
        assert(raster[0] == -1 && raster[width * height - 1] == -1);
        assert(raster[width / 2] == Black);
        assert(raster[width + 40] > raster[width + width - 41]);
        assert(raster[(height - 2) * width + width - 41] > raster[(height - 2) * width + 40]);
        assert(raster[(height / 2) * width + width / 2] == Black);
      }
    }
  }
  assert(componentBit(DarkGray, true) && !componentBit(DarkGray, false));
  assert(componentBit(LightGray, false) && !componentBit(LightGray, true));
  assert(!componentBit(White, true) && !componentBit(Black, false));
  assert(clip({INT_MAX, INT_MAX, INT_MAX, INT_MAX}, 540, 960).width == 0);
  assert(clip({530, 950, INT_MAX, INT_MAX}, 540, 960).width == 10);
  assert(clip({0, 0, -1, 20}, 540, 960).height == 0);
  std::cout << "PASS: " << layouts << " layouts, curved raster bounds, opposed rim tones and solid-black face\n";
}
