#include "components/ReadingCardStyle.h"
#include <cstdio>
#include <vector>
// Raster of the production chrome only; not a hardware or font simulation.
int main() {
  constexpr int width = 540, height = 390;
  std::vector<unsigned char> pixels(width * height, 255);
  auto l = ReadingCardStyle::layout({0, 20, 540, 350}, 20, 300, 0, 0);
  ReadingCardStyle::paint(l.card, l.radius, [&](int x, int y, ReadingCardStyle::Tone tone) {
    pixels[(l.card.y + y) * width + l.card.x + x] = static_cast<unsigned char>(tone * 85);
  });
  std::printf("P5\n%d %d\n255\n", width, height);
  std::fwrite(pixels.data(), 1, pixels.size(), stdout);
}
