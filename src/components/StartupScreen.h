#pragma once

#include <GfxRenderer.h>

#include "FontAwesomeIcons.h"
#include "fontIds.h"

// Caller owns RenderLock. boot() initializes the display, then animates on a
// worker while the owner continues startup; the worker never uses the renderer.
namespace StartupScreen {
// boot() arms one fade for whichever normal startup destination renders first.
// armBootFade() is idempotent and retained for Home's explicit handoff.
void boot(GfxRenderer& renderer);
// Present the existing complete logo once. No animation worker, takeover,
// loading state or boot handoff is started; the caller retains display ownership.
void staticLogo(GfxRenderer& renderer);
void armBootFade();
// Input must not activate a destination before the loading-screen handoff.
bool isLoading();
// Call with RenderLock after the destination has presented its first frame.
void destinationReady();
// Called before the first destination render. False retains ownership for retry.
bool finishBoot(GfxRenderer& renderer);

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
  renderer.displayBuffer(DisplayPresentMode::Quality);
  renderer.setRenderMode(mode);
}
}  // namespace StartupScreen
