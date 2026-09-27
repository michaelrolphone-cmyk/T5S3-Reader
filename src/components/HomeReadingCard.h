#pragma once

#include "components/themes/BaseTheme.h"

// The single-book Home widget shares its chrome across themes. Multi-cover
// themes retain their own layout and selection behavior.
namespace HomeReadingCard {
void draw(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
          int selectorIndex, bool& coverRendered, bool& coverBufferStored,
          bool& bufferRestored, std::function<bool()> storeCoverBuffer);
}
