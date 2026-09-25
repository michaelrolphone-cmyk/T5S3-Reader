#include "StartupScreen.h"

#include <Arduino.h>

namespace StartupScreen {
namespace {
bool fadePending = false;  // Accessed only while holding RenderLock.
constexpr unsigned long kAnimationBudgetMs = 2500;
constexpr int kLogoSize = 240;
constexpr int kFrameHeight = 320;

void drawBootFrame(GfxRenderer& renderer, int rows) {
  renderer.clearScreen();
  // Geometry from docs/logo.svg, enlarged twofold for the monochrome panel.
  struct Rect { int x, y, width, height; };
  constexpr Rect rectangles[] = {
      {10, 14, 16, 16}, {31, 14, 16, 16}, {52, 14, 16, 16},
      {73, 14, 16, 16}, {94, 14, 16, 16}, {10, 38, 47, 16},
      {63, 38, 47, 16}, {10, 62, 100, 16}, {10, 86, 100, 24}};
  constexpr int rowEnds[] = {5, 7, 8, 9};
  const int x = (renderer.getScreenWidth() - kLogoSize) / 2;
  const int y = (renderer.getScreenHeight() - kFrameHeight) / 2;
  for (int i = 0; i < rowEnds[rows - 1]; ++i) {
    const auto& rect = rectangles[i];
    renderer.fillRoundedRect(x + rect.x * 2, y + rect.y * 2,
                             rect.width * 2, rect.height * 2, 4, Color::Black);
  }
  if (rows == 4) {
    renderer.drawCenteredText(UI_12_FONT_ID, y + 250, "RiscRTE", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(SMALL_FONT_ID, y + 290, "Starting...");
  }
}
}  // namespace

void boot(GfxRenderer& renderer) {
  fadePending = false;
  const auto mode = renderer.getRenderMode();
  renderer.setRenderMode(GfxRenderer::BW);
  const unsigned long start = millis();
  for (int rows = 1; rows <= 4; ++rows) {
    // Slow panels skip intermediate frames, but always finish the full logo.
    if (millis() - start >= kAnimationBudgetMs) rows = 4;
    drawBootFrame(renderer, rows);
    renderer.displayBuffer(rows == 1 ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
    delay(1);  // Real scheduler yield between the bounded panel updates.
  }
  renderer.setRenderMode(mode);
}

void armBootFade() { fadePending = true; }

void finishBoot(GfxRenderer& renderer) {
  if (!fadePending) return;
  fadePending = false;  // One-shot: later Home redraws never replay the splash.
  const auto mode = renderer.getRenderMode();
  renderer.setRenderMode(GfxRenderer::BW);
  const unsigned long start = millis();
  // Ordered white-pixel erasure gives a fade on the monochrome framebuffer,
  // including the wordmark. No grayscale planes or screen snapshots needed.
  constexpr uint8_t bayer[4][4] = {
      {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  const int x = (renderer.getScreenWidth() - kLogoSize) / 2;
  const int y = (renderer.getScreenHeight() - kFrameHeight) / 2;
  for (int threshold : {5, 11, 16}) {
    if (millis() - start >= kAnimationBudgetMs) break;
    drawBootFrame(renderer, 4);
    for (int py = 0; py < kFrameHeight; ++py) {
      for (int px = 0; px < kLogoSize; ++px) {
        if (bayer[py & 3][px & 3] < threshold) renderer.drawPixel(x + px, y + py, false);
      }
      if ((py & 15) == 15) delay(1);
    }
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    delay(1);
  }
  renderer.setRenderMode(mode);
  // Home immediately replaces the faded frame with a clean text refresh.
  renderer.requestNextRefresh(HalDisplay::HALF_REFRESH);
}
}  // namespace StartupScreen
