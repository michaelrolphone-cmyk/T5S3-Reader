// Render the production face code to SVG; also check bounds and minute changes.
#include "util/DeskClockFaces.h"
#include <cassert>
#include <fstream>
#include <sstream>
#include <string>

struct Canvas {
  int w, h;
  std::ostringstream out;
  bool record = false;
  uint64_t fingerprint = 14695981039346656037ULL;
  int getScreenWidth() const { return w; }
  int getScreenHeight() const { return h; }
  void fillRect(int x, int y, int width, int height, bool black = true) {
    assert(x >= 0 && y >= 0 && width >= 0 && height >= 0 && x + width <= w && y + height <= h);
    for (int value : {x, y, width, height, static_cast<int>(black)}) {
      fingerprint ^= static_cast<uint32_t>(value);
      fingerprint *= 1099511628211ULL;
    }
    if (!record) return;
    out << "<rect x='" << x << "' y='" << y << "' width='" << width << "' height='" << height
        << "' fill='" << (black ? "black" : "white") << "'/>\n";
  }

};

int main(int argc, char** argv) {
  assert(argc == 2);
  const char* names[] = {"Digital segments", "Smooth sans", "Classic serif", "Minimal dial", "Railway", "Art Deco"};
  for (int width : {800, 960}) {
    const int height = width == 800 ? 480 : 540;
    for (int face = 0; face < DeskClockFaces::Count; ++face) {
      for (bool use12 : {false, true}) {
        // Full day sweeps check changing hands/digits and all geometry bounds.
        uint64_t previous = 0;
        for (int minute = 0; minute < 1440; ++minute) {
          Canvas c{width, height, {}};
          DeskClockFaces::draw(c, face, minute / 60, minute % 60, use12, true);
          assert(c.fingerprint != previous);
          previous = c.fingerprint;
        }
      }
      Canvas invalid{width, height, {}};
      DeskClockFaces::draw(invalid, face, 0, 0, true, false);
      assert(invalid.fingerprint != 14695981039346656037ULL);
      Canvas c{width, height, {}, true};
      DeskClockFaces::draw(c, face, 10, 8, true, true);
      std::ofstream f(std::string(argv[1]) + "/clock-" + std::to_string(width) + "-" + std::to_string(face) + ".svg");
      f << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width << "' height='" << height << "'>"
        << "<rect width='100%' height='100%' fill='white'/>" << c.out.str()
        << "<g text-anchor='middle' font-family='sans-serif' fill='black'>"
        << "<text x='" << width / 2 << "' y='55' font-size='24'>2026-09-30</text>"
        << "<text x='" << width / 2 << "' y='" << height - 55 << "' font-size='24'>AM</text>"
        << "<text x='" << width / 2 << "' y='" << height - 22 << "' font-size='16'>Press power button to wake</text>"
        << "</g></svg>";
      assert(f.good());
    }
  }
  assert(DeskClockFaces::sanitize(255) == DeskClockFaces::Segments);
  assert(DeskClockFaces::point(100, 100, 50, 0).y == 50);
  assert(DeskClockFaces::point(100, 100, 50, 15).x == 150);
  // Names kept here for the matching preview sheet labels.
  for (const auto* name : names) assert(name[0]);
}
