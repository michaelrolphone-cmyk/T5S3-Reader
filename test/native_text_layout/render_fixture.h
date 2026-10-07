// Deterministic drawing/metrics fixture. No hardware or SD reads are simulated.
#include "NativeTextLayoutCache.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <map>
#include <random>
#include <tuple>

static bool rejectAllocation = false;
void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept {
  if (rejectAllocation) return nullptr;
  try {
    return ::operator new[](bytes);
  } catch (...) {
    return nullptr;
  }
}

struct FontData {
  void* glyphMissHandler = nullptr;
} fontData;
struct EpdFontFamily {
  enum Style { REGULAR };
  const FontData* getData(Style) const { return &fontData; }
};
struct GfxRenderer {
  bool sd = false;
  int height = 4;
  uint64_t generation = 1;
  std::map<int, EpdFontFamily> fonts{{10, {}}};
  void clearScreen() const;
  bool isSdCardFont(int) const { return sd; }
  const auto& getFontMap() const { return fonts; }
  uint64_t getFontLayoutGeneration() const { return generation; }
} r;
struct MappedInputManager {} in;
struct t5_ui_chrome_t {
  const char* title;
};
struct t5_ui_text_view_result_t {
  int max_scroll_lines = 0;
  uint32_t total_lines = 0, visible_lines = 0;
};
struct NativeUiLayout {
  int safeLeft = 0, padding = 1, contentTop = 2, contentBottom = 26;
  int headerBottom = 2, width = 14;
  int safeWidth() const { return width; }
} layout;
struct HitLayout {
  int headerBottom = 0, rowTop = 0, rowHeight = 0;
  int pageStart = 0, pageItems = 0, rowCount = 0;
};
std::vector<std::tuple<std::string, int, int>> frame;
void GfxRenderer::clearScreen() const { frame.clear(); }
NativeUiLayout layoutFor(const GfxRenderer&, const t5_ui_chrome_t*) { return layout; }
void drawChrome(GfxRenderer&, MappedInputManager&, const t5_ui_chrome_t* chrome) {
  frame.emplace_back(chrome ? chrome->title : "null", 0, 0);
}
bool presentNativeAppUiFrame() {
  frame.emplace_back("PRESENT", 0, 0);
  return true;
}
bool active = true;
GfxRenderer* renderer() { return active ? &r : nullptr; }
MappedInputManager* input() { return active ? &in : nullptr; }
const char* safe(const char* value) { return value ? value : ""; }
constexpr int UI_10_FONT_ID = 10;
enum class TextRole { UserContent };
int effectiveFont = 10, wraps = 0;
size_t resolverCursor = 0;
std::vector<int> resolverResults{0};
std::vector<std::pair<std::string, int>> resolverTrace;
const char* resolutionPhase = "key";
struct Settings {
  char sdFontFamilyName[32]{};
  int getUserContentFontId() const {
    if (!sdFontFamilyName[0]) return effectiveFont;
    const int result = resolverResults[resolverCursor++ % resolverResults.size()];
    resolverTrace.emplace_back(resolutionPhase, result);
    return result;
  }
} SETTINGS;
uint32_t ticks = 0, increment = 0;
uint32_t millis() {
  ticks += increment;
  return ticks;
}
void vTaskDelay(int) { ++ticks; }
struct BaseTheme {
  // The runner links the verbatim production resolution method below this fixture.
  static int resolveTextFontId(int systemFontId, TextRole role);
  static int resolveForPhase(int systemFontId, TextRole role, const char* phase) {
    resolutionPhase = phase;
    const int fontId = resolveTextFontId(systemFontId, role);
    resolutionPhase = "key";
    return fontId;
  }
  static int getLineHeightForRole(const GfxRenderer& renderer, int systemFontId, TextRole role) {
    const int fontId = resolveForPhase(systemFontId, role, "height");
    return renderer.height + (fontId == UI_10_FONT_ID ? 0 : 1);
  }
  static std::vector<std::string> wrappedTextForRole(const GfxRenderer&, int systemFontId, TextRole role,
                                                   const char* text, int width, int max) {
    const int fontId = resolveForPhase(systemFontId, role, "wrap");
    ++wraps;
    std::vector<std::string> output;
    std::string all = text;
    const size_t length = std::max(1, width / (fontId == UI_10_FONT_ID ? 1 : 2));
    for (size_t i = 0; i < all.size() && output.size() < size_t(max); i += length) {
      output.push_back(all.substr(i, length));
    }
    return output;
  }
  static void drawTextForRole(const GfxRenderer&, int systemFontId, TextRole role,
                              int x, int y, const char* text) {
    const int fontId = resolveForPhase(systemFontId, role, "draw");
    frame.emplace_back(std::to_string(fontId) + ":" + text, x, y);
  }
};
