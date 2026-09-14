#include "components/FontAwesomeIcons.h"
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <SdCardFont.h>
#include <cassert>
#include <string>

int main(int argc, char** argv) {
  assert(argc == 2);
  Storage.root = argv[1];
  // Reproduce the failure using the real SD loader and the shipped cpfont.
  SdCardFont font;
  assert(font.load("/.fonts/FAClassicSolid/FAClassicSolid_18.cpfont"));
  assert(font.prewarm("\xef\x80\x93", 1) == 1); // Gear exists; U+FFFD does not.
  assert(font.getEpdFont(0)->getGlyph(0xf013));
  assert(!font.getEpdFont(0)->getGlyph(0xfffd));

  // Exercise the production draw helper, retaining only a fake display driver.
  static GfxRenderer renderer;
  for (auto size : {12, 14, 16, 18}) {
    const int before = renderer.pixels;
    assert(FontAwesomeIcons::draw(renderer, 0, 0, "solid:f013", size));
    assert(renderer.pixels > before);
    assert(FontAwesomeIcons::draw(renderer, 0, 0, "regular:f017", size));
  }
  assert(renderer.pixels > 0 && renderer.placeholders == 0);
  const int pixels = renderer.pixels;
  // A glyph absent from both families must still use the placeholder.
  assert(!FontAwesomeIcons::draw(renderer, 0, 0, "regular:10ffff", 18));
  assert(renderer.pixels == pixels && renderer.placeholders == 1);
}
