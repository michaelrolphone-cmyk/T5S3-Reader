#pragma once
#include <stdint.h>
class GfxRenderer;
namespace FontAwesomeIcons {
// Call under the normal render lock. x/y is the top-left of the glyph cell.
// Prefers FAClassicRegular and falls back to FAClassicSolid for glyphs that do
// not exist in Regular. Fonts are loaded independently of the selected reader
// family from /.fonts, /fonts, /SD_fonts, or direct family folders at SD root.
// Missing fonts/glyphs draw an outlined placeholder and return false.
bool draw(GfxRenderer& renderer, int x, int y, const char* icon, uint8_t pointSize = 18, bool black = true);
}
