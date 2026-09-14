#include "FontAwesomeIcons.h"
#include <AppManifestRules.h>
#include <GfxRenderer.h>
#include <SdCardFont.h>
#include <cstdio>
#include <memory>

namespace FontAwesomeIcons {
namespace {
struct Slot {
  std::unique_ptr<SdCardFont> font;
  int size = 0;
  GfxRenderer* owner = nullptr;
};

// Keep the Font Awesome caches independent from the selected reading font.
// Index 0 is Solid and index 1 is Regular.
Slot slots[2];

int fontId(bool regular) { return regular ? 0x46410002 : 0x46410001; }
const char* familyName(bool regular) { return regular ? "FAClassicRegular" : "FAClassicSolid"; }

bool ensureFont(GfxRenderer& renderer, bool regular, int size) {
  Slot& slot = slots[regular ? 1 : 0];
  const int id = fontId(regular);

  if (slot.font && (slot.size != size || slot.owner != &renderer)) {
    if (slot.owner) slot.owner->removeFont(id);
    slot.font.reset();
    slot.size = 0;
    slot.owner = nullptr;
  }

  if (slot.font) return true;

  auto font = std::unique_ptr<SdCardFont>(new (std::nothrow) SdCardFont());
  if (!font) return false;

  const char* family = familyName(regular);
  char path[160];
  bool loaded = false;

  // Normal installed-font roots plus the repository's copy-ready SD_fonts
  // layout and direct family folders at the SD root.
  for (const char* root : {"/.fonts", "/fonts", "/SD_fonts", ""}) {
    if (root[0]) {
      snprintf(path, sizeof(path), "%s/%s/%s_%d.cpfont", root, family, family, size);
    } else {
      snprintf(path, sizeof(path), "/%s/%s_%d.cpfont", family, family, size);
    }
    if (font->load(path)) {
      loaded = true;
      break;
    }
  }

  if (!loaded) return false;

  slot.font = std::move(font);
  slot.size = size;
  slot.owner = &renderer;
  renderer.registerSdCardFont(id, slot.font.get());
  renderer.insertFont(id, EpdFontFamily(slot.font->getEpdFont(0)));
  return true;
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

bool drawWithFamily(GfxRenderer& renderer, int x, int y, uint32_t cp, int size, bool regular, bool black) {
  if (!ensureFont(renderer, regular, size)) return false;

  Slot& slot = slots[regular ? 1 : 0];
  char utf8[5];
  encodeUtf8(cp, utf8);

  if (slot.font->prewarm(utf8, 1) != 0) return false;
  EpdFont* epd = slot.font->getEpdFont(0);
  if (!epd) return false;
  const EpdGlyph* glyph = epd->getGlyph(cp);

  // The generated FA fonts contain zero-width placeholders for codepoints not
  // provided by that style. Treat those as missing so the other family can be
  // tried instead of reporting a successful but invisible draw.
  if (!glyph || glyph->width == 0 || glyph->height == 0 || glyph->dataLength == 0) return false;

  renderer.drawText(fontId(regular), x, y, utf8, black);
  return true;
}
}  // namespace

bool draw(GfxRenderer& renderer, int x, int y, const char* icon, uint8_t pointSize, bool black) {
  bool manifestRegular = false;
  uint32_t cp = 0;
  if (!t5_parse_icon(icon, &manifestRegular, &cp)) return false;
  (void)manifestRegular;  // Regular is intentionally preferred regardless of the manifest's legacy style prefix.

  const int size = pointSize <= 12 ? 12 : pointSize <= 14 ? 14 : pointSize <= 16 ? 16 : 18;

  // Prefer FAClassicRegular for springboard/core UI icons. Some Font Awesome
  // glyphs exist only in Solid, so retain Solid as a transparent fallback.
  if (drawWithFamily(renderer, x, y, cp, size, true, black)) return true;
  if (drawWithFamily(renderer, x, y, cp, size, false, black)) return true;

  renderer.drawRect(x, y, size, size, black);
  return false;
}
}  // namespace FontAwesomeIcons
