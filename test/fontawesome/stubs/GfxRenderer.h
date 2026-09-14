#pragma once
#include <SdCardFont.h>
#include <cassert>
class GfxRenderer {
 public:
  int pixels = 0, placeholders = 0;
  const uint8_t* getGlyphBitmap(const EpdFontData* data, const EpdGlyph* glyph) const {
    assert(!data->groups);
    if (data->glyphMissCtx) {
      auto* font = SdCardFont::fromMissCtx(data->glyphMissCtx);
      if (font->isOverflowGlyph(glyph)) return font->getOverflowBitmap(glyph);
    }
    return &data->bitmap[glyph->dataOffset];
  }
  void drawPixel(int, int, bool) { ++pixels; }
  void drawRect(int, int, int, int, bool) { ++placeholders; }
};
