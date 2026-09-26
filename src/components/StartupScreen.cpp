#include "StartupScreen.h"

#include <Arduino.h>

namespace StartupScreen {
namespace {

bool fadePending = false;  // Accessed only while holding RenderLock.

constexpr unsigned long kRevealBudgetMs = 3500;

constexpr int kLogoSize = 240;
constexpr int kFrameHeight = 320;

void drawBootFrame(GfxRenderer& renderer, int rows) {
  renderer.clearScreen();

  // Geometry from docs/logo.svg, enlarged twofold for the monochrome panel.
  struct Rect {
    int x;
    int y;
    int width;
    int height;
  };

  constexpr Rect rectangles[] = {
      {10, 14, 16, 16},
      {31, 14, 16, 16},
      {52, 14, 16, 16},
      {73, 14, 16, 16},
      {94, 14, 16, 16},
      {10, 38, 47, 16},
      {63, 38, 47, 16},
      {10, 62, 100, 16},
      {10, 86, 100, 24},
  };

  constexpr int rowEnds[] = {
      5,
      7,
      8,
      9,
  };

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

  //
  // Initial panel refresh
  //
  // Do this BEFORE starting the reveal timer. HALF_REFRESH can be relatively
  // slow on the e-paper panel, and previously that time consumed the entire
  // animation budget before the reveal animation even began.
  //
  drawBootFrame(renderer, 1);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);

  delay(1);

  //
  // Reveal timing begins only after the initial panel refresh has completed.
  //
  const unsigned long revealStart = millis();

  for (int rows = 2; rows <= 4; ++rows) {
    //
    // If the intermediate FAST_REFRESH frames themselves are unusually slow,
    // skip remaining intermediate rows and finish with the complete logo.
    //
    // The initial HALF_REFRESH can no longer trigger this condition.
    //
    if (rows < 4 &&
        millis() - revealStart >= kRevealBudgetMs) {
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
  if (!fadePending) {
    return;
  }

  // One-shot. Later Home redraws must never replay the splash fade.
  fadePending = false;

  const auto mode = renderer.getRenderMode();
  renderer.setRenderMode(GfxRenderer::BW);

  //
  // 4x4 Bayer ordered-dither matrix.
  //
  // We erase progressively more pixels from the complete logo at each stage.
  //
  constexpr uint8_t bayer[4][4] = {
      {0,  8,  2, 10},
      {12, 4, 14,  6},
      {3, 11,  1,  9},
      {15, 7, 13,  5},
  };

  const int x = (renderer.getScreenWidth() - kLogoSize) / 2;
  const int y = (renderer.getScreenHeight() - kFrameHeight) / 2;

  //
  // Exactly three visible fade stages.
  //
  //   4  = 25% erased
  //   8  = 50% erased
  //   12 = 75% erased
  //
  // Do NOT put an elapsed-time cutoff around these frames. A physical
  // e-paper FAST_REFRESH can take long enough to exhaust a short timer,
  // which previously caused the fade to render one frame and then jump
  // directly to Home.
  //
  constexpr int fadeThresholds[] = {
      4,
      8,
      12,
  };

  for (const int threshold : fadeThresholds) {
    //
    // Reconstruct the complete splash in the framebuffer first. Each fade
    // frame is therefore deterministic rather than accumulating pixel errors
    // from the preceding frame.
    //
    drawBootFrame(renderer, 4);

    for (int py = 0; py < kFrameHeight; ++py) {
      for (int px = 0; px < kLogoSize; ++px) {
        if (bayer[py & 3][px & 3] < threshold) {
          renderer.drawPixel(
              x + px,
              y + py,
              false);
        }
      }

      //
      // Keep the scheduler responsive without adding the large amount of
      // delay that the old every-16-row yield introduced.
      //
      if ((py & 63) == 63) {
        delay(1);
      }
    }

    //
    // Every fade stage is guaranteed to reach the panel.
    //
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }

  renderer.setRenderMode(mode);

  //
  // The splash ends at 75% erased. Home immediately replaces it, producing
  // the final disappearance without wasting another FAST_REFRESH on a fully
  // blank splash frame.
  //
  renderer.requestNextRefresh(HalDisplay::HALF_REFRESH);
}

}  // namespace StartupScreen
