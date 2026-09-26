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
// Home/menu presentation variant: prefer the FA Regular (outline) face. If
// that codepoint has no Regular glyph, render only the boundary pixels of the
// Solid glyph so app shortcuts remain outlined rather than filled.
bool drawOutline(GfxRenderer& renderer, int x, int y, const char* icon,
                 uint8_t pointSize = 18, bool black = true);
}
