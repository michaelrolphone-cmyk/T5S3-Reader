#include "StartupScreen.h"

#include <Arduino.h>

namespace StartupScreen {
namespace {
bool fadePending = false;  // Accessed only while holding RenderLock.

constexpr unsigned long kRevealBudgetMs = 3000;
constexpr unsigned long kFadeBudgetMs = 875;  // 4x faster than reveal budget.

constexpr int kLogoSize = 240;
constexpr int kFrameHeight = 320;

void drawBootFrame(GfxRenderer& renderer, int rows) {
  renderer.clearScreen();

  // Geometry from docs/logo.svg, enlarged twofold for the monochrome panel.
  struct Rect {
    int x, y, width, height;
  };

  constexpr Rect rectangles[] = {
      {10, 14, 16, 16}, {31, 14, 16, 16}, {52, 14, 16, 16},
      {73, 14, 16, 16}, {94, 14, 16, 16}, {10, 38, 47, 16},
      {63, 38, 47, 16}, {10, 62, 100, 16}, {10, 86, 100, 24}};

  constexpr int rowEnds[] = {5, 7, 8, 9};

  const int x = (renderer.getScreenWidth() - kLogoSize) / 2;
  const int y = (renderer.getScreenHeight() - kFrameHeight) / 2;

  for (int i = 0; i < rowEnds[rows - 1]; ++i) {
    const auto& rect = rectangles[i];
    renderer.fillRoundedRect(
        x + rect.x * 2,
        y + rect.y * 2,
        rect.width * 2,
        rect.height * 2,
        4,
        Color::Black);
  }

  if (rows == 4) {
    renderer.drawCenteredText(
        UI_12_FONT_ID,
        y + 250,
        "RiscRTE",
        true,
        EpdFontFamily::BOLD);

    renderer.drawCenteredText(
        SMALL_FONT_ID,
        y + 290,
        "Starting...");
  }
}

}  // namespace

void boot(GfxRenderer& renderer) {
  fadePending = false;

  const auto mode = renderer.getRenderMode();
  renderer.setRenderMode(GfxRenderer::BW);

  // Initial frame uses the expensive refresh, but it is deliberately
  // outside the reveal animation budget.
  drawBootFrame(renderer, 1);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  delay(1);

  // Reveal timing starts only after the initial physical refresh completes.
  const unsigned long revealStart = millis();

  for (int rows = 2; rows <= 4; ++rows) {
    // If the remaining panel updates are unusually slow, skip only the
    // remaining intermediate reveal frames. The initial refresh can no
    // longer force us directly from blank to the complete logo.
    if (rows < 4 && millis() - revealStart >= kRevealBudgetMs) {
      rows = 4;
    }

    drawBootFrame(renderer, rows);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    delay(1);
  }

  renderer.setRenderMode(mode);
}

void armBootFade() {
  fadePending = true;
}

void finishBoot(GfxRenderer& renderer) {
  if (!fadePending) return;

  fadePending = false;  // One-shot: later Home redraws never replay splash.

  const auto mode = renderer.getRenderMode();
  renderer.setRenderMode(GfxRenderer::BW);

  constexpr uint8_t bayer[4][4] = {
      {0, 8, 2, 10},
      {12, 4, 14, 6},
      {3, 11, 1, 9},
      {15, 7, 13, 5}};

  const int x = (renderer.getScreenWidth() - kLogoSize) / 2;
  const int y = (renderer.getScreenHeight() - kFrameHeight) / 2;

  const unsigned long fadeStart = millis();

  // Two stages instead of three. The first removes roughly half the logo;
  // the second removes it completely. This avoids spending several e-paper
  // refresh cycles slowly stepping through the fade.
  for (int threshold : {8, 16}) {
    drawBootFrame(renderer, 4);

    for (int py = 0; py < kFrameHeight; ++py) {
      for (int px = 0; px < kLogoSize; ++px) {
        if (bayer[py & 3][px & 3] < threshold) {
          renderer.drawPixel(x + px, y + py, false);
        }
      }

      // Keep the renderer cooperative without adding significant fade time.
      if ((py & 63) == 63) {
        delay(1);
      }
    }

    renderer.displayBuffer(HalDisplay::FAST_REFRESH);

    if (millis() - fadeStart >= kFadeBudgetMs) {
      break;
    }

    delay(1);
  }

  renderer.setRenderMode(mode);

  // Home immediately replaces the faded frame.
  renderer.requestNextRefresh(HalDisplay::HALF_REFRESH);
}

}  // namespace StartupScreen
