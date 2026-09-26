#include "FontAwesomeIcons.h"
#include <AppManifestRules.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <SdCardFont.h>
#include <cstdio>
#include <memory>

namespace FontAwesomeIcons {
namespace {
struct Slot {
  std::unique_ptr<SdCardFont> font;
  int size = 0;
};

// Index 0 is Solid and index 1 is Regular. These fonts are intentionally kept
// separate from the reader's selected SD font and from GfxRenderer's font map.
Slot slots[2];

const char* familyName(bool regular) { return regular ? "FAClassicRegular" : "FAClassicSolid"; }

bool ensureFont(bool regular, int size) {
  Slot& slot = slots[regular ? 1 : 0];
  if (slot.font && slot.size != size) {
    slot.font.reset();
    slot.size = 0;
  }
  if (slot.font) return true;

  auto font = std::unique_ptr<SdCardFont>(new (std::nothrow) SdCardFont());
  if (!font) return false;

  const char* family = familyName(regular);
  char path[128];

  // The project historically documents /.fonts, but some SD images and users
  // have the hidden directory as /.font. Accept both spellings, plus the
  // visible /fonts directory, so a missing icon face never silently degrades
  // to the fallback rectangle solely because of the SD directory name.
  static constexpr const char* kFontRoots[] = {"/.fonts", "/.font", "/fonts"};
  for (const char* root : kFontRoots) {
    snprintf(path, sizeof(path), "%s/%s/%s_%d.cpfont", root, family, family, size);
    if (font->load(path)) {
      LOG_DBG("FA", "Loaded %s size %d from %s", family, size, path);
      slot.font = std::move(font);
      slot.size = size;
      return true;
    }
  }

  LOG_ERR("FA", "Unable to load %s size %d from /.fonts, /.font, or /fonts", family, size);
  return false;
}

void encodeUtf8(uint32_t cp, char utf8[5]) {
  utf8[0] = utf8[1] = utf8[2] = utf8[3] = utf8[4] = 0;
  if (cp < 0x80) {
    utf8[0] = static_cast<char>(cp);
  } else if (cp < 0x800) {
    utf8[0] = static_cast<char>(0xc0 | (cp >> 6));
    utf8[1] = static_cast<char>(0x80 | (cp & 63));
  } else if (cp < 0x10000) {
    utf8[0] = static_cast<char>(0xe0 | (cp >> 12));
    utf8[1] = static_cast<char>(0x80 | ((cp >> 6) & 63));
    utf8[2] = static_cast<char>(0x80 | (cp & 63));
  } else {
    utf8[0] = static_cast<char>(0xf0 | (cp >> 18));
    utf8[1] = static_cast<char>(0x80 | ((cp >> 12) & 63));
    utf8[2] = static_cast<char>(0x80 | ((cp >> 6) & 63));
    utf8[3] = static_cast<char>(0x80 | (cp & 63));
  }
}

bool drawGlyphBitmap(GfxRenderer& renderer, int x, int y, int cellSize, const EpdFontData* data,
                     const EpdGlyph* glyph, bool black) {
  if (!data || !glyph || glyph->width == 0 || glyph->height == 0 || glyph->dataLength == 0) return false;
  const uint8_t* bitmap = renderer.getGlyphBitmap(data, glyph);
  if (!bitmap) return false;

  // draw_icon() defines x/y as the top-left of an icon cell, not a text
  // baseline. Center the rasterized glyph in that cell and draw it directly so
  // Font Awesome does not depend on dynamic registration in GfxRenderer's text
  // font map.
  const int drawX = x + (cellSize - static_cast<int>(glyph->width)) / 2;
  const int drawY = y + (cellSize - static_cast<int>(glyph->height)) / 2;
  int pixelPosition = 0;

  if (data->is2Bit) {
    for (int gy = 0; gy < glyph->height; ++gy) {
      for (int gx = 0; gx < glyph->width; ++gx, ++pixelPosition) {
        const uint8_t byte = bitmap[pixelPosition >> 2];
        const uint8_t shift = static_cast<uint8_t>((3 - (pixelPosition & 3)) * 2);
        const uint8_t coverage = (byte >> shift) & 0x3;
        if (coverage != 0) renderer.drawPixel(drawX + gx, drawY + gy, black);
      }
    }
  } else {
    for (int gy = 0; gy < glyph->height; ++gy) {
      for (int gx = 0; gx < glyph->width; ++gx, ++pixelPosition) {
        const uint8_t byte = bitmap[pixelPosition >> 3];
        const uint8_t shift = static_cast<uint8_t>(7 - (pixelPosition & 7));
        if ((byte >> shift) & 1) renderer.drawPixel(drawX + gx, drawY + gy, black);
      }
    }
  }
  return true;
}

bool drawWithFamily(GfxRenderer& renderer, int x, int y, uint32_t cp, int size, bool regular, bool black) {
  if (!ensureFont(regular, size)) return false;

  Slot& slot = slots[regular ? 1 : 0];
  char utf8[5];
  encodeUtf8(cp, utf8);

  // prewarm also requests U+FFFD, which Font Awesome intentionally omits.
  // Positive miss counts can therefore accompany a successfully loaded icon.
  // Check the requested glyph below; only negative results are fatal here.
  if (slot.font->prewarm(utf8, 1) < 0) {
    LOG_DBG("FA", "%s failed to prepare U+%04lX", familyName(regular), static_cast<unsigned long>(cp));
    return false;
  }

  EpdFont* epd = slot.font->getEpdFont(0);
  if (!epd) return false;
  const EpdGlyph* glyph = epd->getGlyph(cp);
  if (!glyph || glyph->width == 0 || glyph->height == 0 || glyph->dataLength == 0) {
    LOG_DBG("FA", "%s glyph U+%04lX is empty", familyName(regular), static_cast<unsigned long>(cp));
    return false;
  }

  return drawGlyphBitmap(renderer, x, y, size, epd->data, glyph, black);
}
}  // namespace

bool draw(GfxRenderer& renderer, int x, int y, const char* icon, uint8_t pointSize, bool black) {
  bool manifestRegular = false;
  uint32_t cp = 0;
  if (!t5_parse_icon(icon, &manifestRegular, &cp)) return false;

  const int size = pointSize <= 12 ? 12 : pointSize <= 14 ? 14 : pointSize <= 16 ? 16 : 18;

  // Honor the manifest face first. This matters for glyphs whose regular and
  // solid forms share a codepoint. If that face does not contain the glyph,
  // fall back to the other face rather than drawing an empty placeholder.
  if (drawWithFamily(renderer, x, y, cp, size, manifestRegular, black)) return true;
  if (drawWithFamily(renderer, x, y, cp, size, !manifestRegular, black)) return true;

  LOG_ERR("FA", "No Font Awesome glyph for '%s' (U+%04lX)", icon ? icon : "", static_cast<unsigned long>(cp));
  renderer.drawRect(x, y, size, size, black);
  return false;
}

bool drawRegular(GfxRenderer& renderer, int x, int y, const char* icon, uint8_t pointSize, bool black) {
  bool ignoredManifestRegular = false;
  uint32_t cp = 0;
  if (!t5_parse_icon(icon, &ignoredManifestRegular, &cp)) return false;

  const int size = pointSize <= 12 ? 12 : pointSize <= 14 ? 14 : pointSize <= 16 ? 16 : 18;
  if (drawWithFamily(renderer, x, y, cp, size, true, black)) return true;

  LOG_ERR("FA", "No Font Awesome Regular glyph for '%s' (U+%04lX)",
          icon ? icon : "", static_cast<unsigned long>(cp));
  renderer.drawRect(x, y, size, size, black);
  return false;
}
}  // namespace FontAwesomeIcons
