#include "FontAwesomeIcons.h"
#include <AppManifestRules.h>
#include <GfxRenderer.h>
#include <SdCardFont.h>
#include <cstdio>
#include <memory>

namespace FontAwesomeIcons {
bool draw(GfxRenderer& renderer, int x, int y, const char* icon, uint8_t pointSize, bool black) {
  bool regular = false;
  uint32_t cp = 0;
  if (!t5_parse_icon(icon, &regular, &cp)) return false;
  const int size = pointSize <= 12 ? 12 : pointSize <= 14 ? 14 : pointSize <= 16 ? 16 : 18;
  // One cached size per style, independent of the selected reading font.
  struct Slot { std::unique_ptr<SdCardFont> font; int size = 0; GfxRenderer* owner = nullptr; };
  static Slot slots[2];
  Slot& slot = slots[regular ? 1 : 0];
  const int id = regular ? 0x46410002 : 0x46410001;
  if (slot.font && (slot.size != size || slot.owner != &renderer)) {
    slot.owner->removeFont(id);
    slot.font.reset();
  }
  if (!slot.font) {
    auto font = std::unique_ptr<SdCardFont>(new (std::nothrow) SdCardFont());
    const char* family = regular ? "FAClassicRegular" : "FAClassicSolid";
    char path[128];
    bool loaded = false;
    for (const char* root : {"/.fonts", "/fonts"}) {
      snprintf(path, sizeof(path), "%s/%s/%s_%d.cpfont", root, family, family, size);
      if (font && font->load(path)) { loaded = true; break; }
    }
    if (loaded) {
      slot.font = std::move(font);
      slot.size = size;
      slot.owner = &renderer;
      renderer.registerSdCardFont(id, slot.font.get());
      renderer.insertFont(id, EpdFontFamily(slot.font->getEpdFont(0)));
    }
  }
  char utf8[5] = {};
  if (cp < 0x80) utf8[0] = cp;
  else if (cp < 0x800) { utf8[0] = 0xc0 | (cp >> 6); utf8[1] = 0x80 | (cp & 63); }
  else if (cp < 0x10000) {
    utf8[0] = 0xe0 | (cp >> 12); utf8[1] = 0x80 | ((cp >> 6) & 63); utf8[2] = 0x80 | (cp & 63);
  } else {
    utf8[0] = 0xf0 | (cp >> 18); utf8[1] = 0x80 | ((cp >> 12) & 63);
    utf8[2] = 0x80 | ((cp >> 6) & 63); utf8[3] = 0x80 | (cp & 63);
  }
  if (slot.font && slot.font->prewarm(utf8, 1) == 0 && slot.font->getEpdFont(0)->getGlyph(cp)) {
    renderer.drawText(id, x, y, utf8, black);
    return true;
  }
  renderer.drawRect(x, y, size, size, black);
  return false;
}
}
