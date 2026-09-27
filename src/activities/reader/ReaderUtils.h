#pragma once

#include <CrossPointSettings.h>
#include <GfxRenderer.h>
#include <HalTiltSensor.h>
#include <Logging.h>

#include "activities/RenderLock.h"
#include "MappedInputManager.h"

namespace ReaderUtils {

constexpr unsigned long GO_HOME_MS = 1000;
constexpr unsigned long BOOKMARK_HOLD_MS = 400;
constexpr unsigned long BOOKMARK_MESSAGE_DURATION_MS = 2500;

inline void applyOrientation(GfxRenderer& renderer, const uint8_t orientation) {
  switch (orientation) {
    case CrossPointSettings::ORIENTATION::PORTRAIT:
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
      break;
    case CrossPointSettings::ORIENTATION::INVERTED:
      renderer.setOrientation(GfxRenderer::Orientation::PortraitInverted);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CCW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
      break;
    default:
      break;
  }
}

struct PageTurnResult {
  bool prev;
  bool next;
  bool fromTilt;
};

inline PageTurnResult detectPageTurn(const MappedInputManager& input) {
  const bool usePress = SETTINGS.longPressButtonBehavior == SETTINGS.OFF;
  const bool tiltNext = SETTINGS.tiltPageTurn && halTiltSensor.wasTiltedForward();
  const bool tiltPrev = SETTINGS.tiltPageTurn && halTiltSensor.wasTiltedBack();
  const bool prev = tiltPrev || (usePress ? (input.wasPressed(MappedInputManager::Button::PageBack) ||
                                             input.wasPressed(MappedInputManager::Button::Left))
                                          : (input.wasReleased(MappedInputManager::Button::PageBack) ||
                                             input.wasReleased(MappedInputManager::Button::Left)));
  const bool next = tiltNext || (usePress ? (input.wasPressed(MappedInputManager::Button::PageForward) ||
                                             input.wasPressed(MappedInputManager::Button::Right))
                                          : (input.wasReleased(MappedInputManager::Button::PageForward) ||
                                             input.wasReleased(MappedInputManager::Button::Right)));
  return {prev, next, tiltPrev || tiltNext};
}

inline DisplayPresentMode getReaderDisplayRefreshMode() {
  switch (SETTINGS.readerDisplayMode) {
    case CrossPointSettings::READER_DISPLAY_FAST:
      return DisplayPresentMode::LowLatency;
    case CrossPointSettings::READER_DISPLAY_STANDARD:
      return DisplayPresentMode::Balanced;
    case CrossPointSettings::READER_DISPLAY_QUALITY:
    default:
      return DisplayPresentMode::Quality;
  }
}

inline bool shouldAnimatePageTurn() {
  return SETTINGS.readerDisplayMode != CrossPointSettings::READER_DISPLAY_QUALITY;
}

inline bool isPageTurnInputBlocked() {
  // Keep page-turn input from stacking while the current reader update or
  // page-turn effect is still being pushed to the panel.
  return RenderLock::peek();
}

inline void requestPageTurnEffect(const GfxRenderer& renderer, const bool isForwardTurn) {
  switch (SETTINGS.readerDisplayMode) {
    case CrossPointSettings::READER_DISPLAY_FAST:
      renderer.requestNextDisplayEffect(isForwardTurn ? DisplayEffect::PageTurnForwardFast
                                                      : DisplayEffect::PageTurnBackwardFast);
      return;
    case CrossPointSettings::READER_DISPLAY_STANDARD:
      renderer.requestNextDisplayEffect(isForwardTurn ? DisplayEffect::PageTurnForwardStandard
                                                      : DisplayEffect::PageTurnBackwardStandard);
      return;
    case CrossPointSettings::READER_DISPLAY_QUALITY:
    default:
      renderer.requestNextDisplayEffect(DisplayEffect::None);
      return;
  }
}

inline DisplayPresentMode takeReaderRefreshMode(int& pagesUntilFullRefresh) {
  if (pagesUntilFullRefresh <= 1) {
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
    return DisplayPresentMode::Clean;
  }

  pagesUntilFullRefresh--;
  return getReaderDisplayRefreshMode();
}

inline void displayWithRefreshCycle(const GfxRenderer& renderer, int& pagesUntilFullRefresh) {
  renderer.displayBuffer(takeReaderRefreshMode(pagesUntilFullRefresh));
}

inline void markRefreshCycleDisplayed(int& pagesUntilFullRefresh) {
  (void)takeReaderRefreshMode(pagesUntilFullRefresh);
}

inline int initialPagesUntilFullRefresh(const DisplayPresentMode initialRefreshMode) {
  return initialRefreshMode == DisplayPresentMode::Clean ? 0 : SETTINGS.getRefreshFrequency();
}

// Grayscale anti-aliasing pass. The caller renders the BW page first,
// captures it, renders gray planes, then updates the panel once.
// Only the content callback is re-rendered; status bars stay in the captured
// BW base so they are not updated twice.
// Kept as a template to avoid std::function overhead; instantiated once per reader type.
template <typename RenderFn>
void renderAntiAliased(GfxRenderer& renderer, int& pagesUntilFullRefresh, RenderFn&& renderFn) {
  if (!renderer.captureGrayscaleBaseBuffer()) {
    LOG_ERR("READER", "Failed to capture BW buffer for anti-aliasing");
    displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
    return;
  }

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  renderFn();
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderFn();
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer(takeReaderRefreshMode(pagesUntilFullRefresh));
  renderer.setRenderMode(GfxRenderer::BW);
}

}  // namespace ReaderUtils
