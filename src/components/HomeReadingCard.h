#pragma once

#include "components/themes/BaseTheme.h"

// Single-book Home widget. Multi-cover themes keep their own layout.
namespace HomeReadingCard {
void draw(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
          int selectorIndex, bool& coverRendered, bool& coverBufferStored,
          bool& bufferRestored, std::function<bool()> storeCoverBuffer);
// Call after the complete Home frame (including menus) has been drawn. Returns
// true for a gray presentation, false for a safe ordinary-BW presentation.
bool present(GfxRenderer& renderer, Rect rect);
}
