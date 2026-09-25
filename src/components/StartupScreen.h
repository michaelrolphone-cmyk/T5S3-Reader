#pragma once

#include <GfxRenderer.h>

#include "FontAwesomeIcons.h"
#include "fontIds.h"

// Caller owns RenderLock. These frames are presented synchronously, before
// entering work that can block the activity render task.
namespace StartupScreen {
inline void boot(GfxRenderer& renderer) {
  renderer.clearScreen();
  // Native vector rendering of docs/logo.svg (120 x 120 viewBox), enlarged
  // twofold. Its CSS palette becomes black for the monochrome boot surface.
  // Keep the SVG's rectangles and rounded corners; no SD asset/ELF is needed.
  struct Rect { int x, y, width, height; };
  constexpr Rect rectangles[] = {
      {10, 14, 16, 16}, {31, 14, 16, 16}, {52, 14, 16, 16},
      {73, 14, 16, 16}, {94, 14, 16, 16}, {10, 38, 47, 16},
      {63, 38, 47, 16}, {10, 62, 100, 16}, {10, 86, 100, 24}};
  const int x = (renderer.getScreenWidth() - 240) / 2;
  const int y = (renderer.getScreenHeight() - 320) / 2;
  for (const auto& rect : rectangles) {
    renderer.fillRoundedRect(x + rect.x * 2, y + rect.y * 2,
                             rect.width * 2, rect.height * 2, 4, Color::Black);
  }
  renderer.drawCenteredText(UI_12_FONT_ID, y + 250, "RiscRTE", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, y + 290, "Starting...");
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

inline void app(GfxRenderer& renderer, const char* name, const char* icon) {
  const auto mode = renderer.getRenderMode();
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.clearScreen();
  constexpr int iconCell = 18;
  const int centerY = renderer.getScreenHeight() / 2;
  FontAwesomeIcons::draw(renderer, (renderer.getScreenWidth() - iconCell) / 2,
                         centerY - 64, icon, iconCell);
  const std::string message = std::string("Loading ") + name;
  const auto label = renderer.truncatedText(UI_12_FONT_ID, message.c_str(), renderer.getScreenWidth() - 48);
  renderer.drawCenteredText(UI_12_FONT_ID, centerY, label.c_str());
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  renderer.setRenderMode(mode);
}
}  // namespace StartupScreen
