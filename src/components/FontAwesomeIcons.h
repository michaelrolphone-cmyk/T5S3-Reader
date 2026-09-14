#pragma once
#include <stdint.h>
class GfxRenderer;
namespace FontAwesomeIcons {
// Call under the normal render lock. x/y is the top-left of the glyph cell.
// Prefers FAClassicRegular and falls back to FAClassicSolid for glyphs that do
// not exist in Regular. Fonts are loaded independently of the selected reader
// family from the standard /.fonts or /fonts SD roots and their glyph bitmaps
// are drawn directly rather than registered as normal UI text fonts.
// Missing fonts/glyphs draw an outlined placeholder and return false.
bool draw(GfxRenderer& renderer, int x, int y, const char* icon, uint8_t pointSize = 18, bool black = true);
}
