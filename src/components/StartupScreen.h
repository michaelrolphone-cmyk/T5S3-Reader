#pragma once

#include <GfxRenderer.h>

#include "FontAwesomeIcons.h"
#include "fontIds.h"

// Caller owns RenderLock. These frames are presented synchronously, before
// entering work that can block the activity render task.
namespace StartupScreen {
// The fade is armed only for a normal Home boot. Caller holds RenderLock.
void boot(GfxRenderer& renderer);
void armBootFade();
void finishBoot(GfxRenderer& renderer);

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
