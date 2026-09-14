#pragma once
#include <stdint.h>
class GfxRenderer;
namespace FontAwesomeIcons {
// Call under the normal render lock. x/y is the top-left of the glyph cell.
// Uses installed FAClassicSolid/FAClassicRegular cpfonts independently of the
// reader family. Missing fonts/glyphs draw an outlined placeholder and return false.
bool draw(GfxRenderer& renderer, int x, int y, const char* icon, uint8_t pointSize = 18, bool black = true);
}
