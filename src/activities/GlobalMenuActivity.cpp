#include "GlobalMenuActivity.h"

#include <Board.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <cstring>
#include <esp_task_wdt.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "PowerControl.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "runtime/memory/PsramBuffer.h"

namespace {
constexpr int kPanelMargin = 10;   // gap from the screen edges
constexpr int kPanelTopGap = 8;    // gap from the very top edge
constexpr int kInnerPad = 16;      // padding inside the panel
constexpr int kButtonHeight = 60;
constexpr int kButtonGap = 14;
constexpr int kCornerRadius = 12;
constexpr int kArrowRegionHeight = 44;  // band below the panel that holds the "hide" up-arrow
constexpr int kArrowHalfWidth = 20;
constexpr int kArrowHeight = 18;

bool hasBacklight() { return Board::capabilities().hasBacklight; }

bool modalShutdownConfirmed(GfxRenderer& renderer, MappedInputManager& input) {
  constexpr int margin = 20;
  constexpr int spacing = 30;
  constexpr int fontId = UI_10_FONT_ID;
  const int lineHeight = renderer.getLineHeight(fontId);
  const int maxWidth = renderer.getScreenWidth() - margin * 2;
  const std::string heading =
      renderer.truncatedText(fontId, I18N.get(StrId::STR_SHUTDOWN), maxWidth, EpdFontFamily::BOLD);
  const std::string body =
      renderer.truncatedText(fontId, I18N.get(StrId::STR_SHUTDOWN_PROMPT), maxWidth, EpdFontFamily::REGULAR);
  int totalHeight = lineHeight * 2 + spacing;
  int y = (renderer.getScreenHeight() - totalHeight) / 2;

  renderer.clearScreen();
  renderer.drawCenteredText(fontId, y, heading.c_str(), true, EpdFontFamily::BOLD);
  y += lineHeight + spacing;
  renderer.drawCenteredText(fontId, y, body.c_str(), true, EpdFontFamily::REGULAR);
  const auto labels = input.mapLabels("", "", I18N.get(StrId::STR_CANCEL), I18N.get(StrId::STR_CONFIRM));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);

  for (;;) {
    esp_task_wdt_reset();
    delay(10);
    input.update();

    if (input.wasTouchHomeButtonPressed() ||
        input.wasReleased(MappedInputManager::Button::Back) ||
        input.wasReleased(MappedInputManager::Button::Left)) {
      return false;
    }
    if (input.wasReleased(MappedInputManager::Button::Right)) return true;

    MappedInputManager::TouchPoint point{};
    if (input.wasTouchTapped(point, renderer))
      return point.x >= renderer.getScreenWidth() / 2;
  }
}
}  // namespace

GlobalMenuActivity::GlobalMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       bool overGrayscaleReader)
    : Activity("GlobalMenu", renderer, mappedInput), overGrayscaleReader(overGrayscaleReader) {}

void GlobalMenuActivity::onEnter() {
  Activity::onEnter();
  if (!hasBacklight()) {
    selectedIndex = BUTTON_SHUTDOWN;
  }
  requestUpdate();
}

void GlobalMenuActivity::getPanelLayout(int& panelX, int& panelY, int& panelW, int& panelH) const {
  const int screenW = renderer.getScreenWidth();
  panelX = kPanelMargin;
  panelY = kPanelTopGap;
  panelW = screenW - kPanelMargin * 2;
  const int visibleButtonCount = hasBacklight() ? BUTTON_COUNT : 1;
  panelH = kInnerPad * 2 + visibleButtonCount * kButtonHeight + (visibleButtonCount - 1) * kButtonGap;
}

void GlobalMenuActivity::getButtonRect(int index, int& x, int& y, int& w, int& h) const {
  int panelX, panelY, panelW, panelH;
  getPanelLayout(panelX, panelY, panelW, panelH);
  x = panelX + kInnerPad;
  w = panelW - kInnerPad * 2;
  h = kButtonHeight;
  const int row = hasBacklight() ? index : 0;
  y = panelY + kInnerPad + row * (kButtonHeight + kButtonGap);
}

void GlobalMenuActivity::loop() {
  const auto moveNext = [this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, BUTTON_COUNT);
    requestUpdate();
  };
  const auto movePrevious = [this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, BUTTON_COUNT);
    requestUpdate();
  };

  if (!hasBacklight()) {
    selectedIndex = BUTTON_SHUTDOWN;
  } else if (selectedIndex == BUTTON_BACKLIGHT) {
    // While the backlight button is focused, Left/Right adjust brightness and only
    // Up/Down navigate, so the two functions don't collide on the same buttons.
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right},
                                         [this] { applyBacklightLevel(SETTINGS.backlightLevel + 1); });
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left},
                                         [this] { applyBacklightLevel(SETTINGS.backlightLevel - 1); });
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, moveNext);
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, movePrevious);
  } else {
    buttonNavigator.onNext(moveNext);
    buttonNavigator.onPrevious(movePrevious);
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelection();
    return;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();  // dismiss
    return;
  }
}

bool GlobalMenuActivity::onTouchTap(int16_t x, int16_t y) {
  int panelX, panelY, panelW, panelH;
  getPanelLayout(panelX, panelY, panelW, panelH);

  // A tap outside the panel (typically below it) dismisses the menu and returns the
  // user to what they were doing.
  const bool insidePanel = x >= panelX && x < panelX + panelW && y >= panelY && y < panelY + panelH;
  if (!insidePanel) {
    finish();
    return true;
  }

  // Backlight button: left half decreases, right half increases (the -/+ glyphs are affordances).
  int bx, by, bw, bh;
  if (hasBacklight()) {
    getButtonRect(BUTTON_BACKLIGHT, bx, by, bw, bh);
    if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
      selectedIndex = BUTTON_BACKLIGHT;
      applyBacklightLevel(x < bx + bw / 2 ? SETTINGS.backlightLevel - 1 : SETTINGS.backlightLevel + 1);
      requestUpdate();
      return true;
    }
  }

  getButtonRect(BUTTON_SHUTDOWN, bx, by, bw, bh);
  if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
    selectedIndex = BUTTON_SHUTDOWN;
    triggerShutdown();
    return true;
  }

  // Tap on the panel background (not a button): keep the menu open.
  return true;
}

void GlobalMenuActivity::activateSelection() {
  if (selectedIndex == BUTTON_BACKLIGHT) {
    // Confirm cycles the shared global backlight level up, wrapping 10 -> 0.
    applyBacklightLevel(SETTINGS.backlightLevel >= 10 ? 0 : SETTINGS.backlightLevel + 1);
    return;
  }
  triggerShutdown();
}

void GlobalMenuActivity::applyBacklightLevel(int level, bool scheduleRender) {
  if (!hasBacklight()) {
    return;
  }
  if (level < 0) level = 0;
  if (level > 10) level = 10;
  if (level == SETTINGS.backlightLevel) {
    return;
  }
  SETTINGS.backlightLevel = static_cast<uint8_t>(level);
  Board::setBacklightLevel(SETTINGS.backlightLevel);
  SETTINGS.saveToFile();
  if (scheduleRender) requestUpdate();
}

void GlobalMenuActivity::triggerShutdown() {
  if (SETTINGS.confirmShutdown) {
    startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, I18N.get(StrId::STR_SHUTDOWN),
                                                                  I18N.get(StrId::STR_SHUTDOWN_PROMPT)),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               requestShutdown();
                             }
                             // Dismiss the menu either way: on cancel this restores the
                             // underlying screen; on confirm the device powers off shortly.
                             finish();
                           });
    return;
  }
  requestShutdown();
}

void GlobalMenuActivity::drawButtonBox(int x, int y, int width, int height, bool focused) {
  if (focused) {
    renderer.fillRoundedRect(x, y, width, height, kCornerRadius, Color::Black);
  } else {
    renderer.drawRoundedRect(x, y, width, height, 2, kCornerRadius, true);
  }
}

void GlobalMenuActivity::drawBacklightButton(int x, int y, int width, int height, bool focused, int level) {
  drawButtonBox(x, y, width, height, focused);
  const bool textBlack = !focused;

  const std::string centerText = std::string(I18N.get(StrId::STR_BACKLIGHT)) + "   " + std::to_string(level);
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int textY = y + (height - lineHeight) / 2;
  const int centerWidth = renderer.getTextWidth(UI_10_FONT_ID, centerText.c_str());
  renderer.drawText(UI_10_FONT_ID, x + (width - centerWidth) / 2, textY, centerText.c_str(), textBlack);

  const int glyphLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int glyphY = y + (height - glyphLineHeight) / 2;
  constexpr int glyphPadding = 26;
  renderer.drawText(UI_12_FONT_ID, x + glyphPadding, glyphY, "-", textBlack, EpdFontFamily::BOLD);
  const int plusWidth = renderer.getTextWidth(UI_12_FONT_ID, "+", EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, x + width - glyphPadding - plusWidth, glyphY, "+", textBlack, EpdFontFamily::BOLD);
}

void GlobalMenuActivity::drawActionButton(int x, int y, int width, int height, bool focused,
                                          const std::string& label) {
  drawButtonBox(x, y, width, height, focused);
  const bool textBlack = !focused;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int textY = y + (height - lineHeight) / 2;
  const int labelWidth = renderer.getTextWidth(UI_10_FONT_ID, label.c_str());
  renderer.drawText(UI_10_FONT_ID, x + (width - labelWidth) / 2, textY, label.c_str(), textBlack);
}

void GlobalMenuActivity::renderOverlay(HalDisplay::RefreshMode refreshMode) {
  // Top-only overlay: paint an opaque white band over just the top of the screen for
  // the panel, and leave everything below untouched so the previous content remains visible.
  int panelX, panelY, panelW, panelH;
  getPanelLayout(panelX, panelY, panelW, panelH);
  const int screenW = renderer.getScreenWidth();
  const int panelBottom = panelY + panelH;
  const int dividerY = panelBottom + kArrowRegionHeight;

  renderer.fillRect(0, 0, screenW, dividerY, false);

  int bx, by, bw, bh;
  if (hasBacklight()) {
    getButtonRect(BUTTON_BACKLIGHT, bx, by, bw, bh);
    drawBacklightButton(bx, by, bw, bh, selectedIndex == BUTTON_BACKLIGHT, SETTINGS.backlightLevel);
  }

  getButtonRect(BUTTON_SHUTDOWN, bx, by, bw, bh);
  drawActionButton(bx, by, bw, bh, selectedIndex == BUTTON_SHUTDOWN, I18N.get(StrId::STR_SHUTDOWN));

  const int arrowCenterX = screenW / 2;
  const int arrowTopY = panelBottom + (kArrowRegionHeight - kArrowHeight) / 2;
  const int arrowXPoints[3] = {arrowCenterX, arrowCenterX - kArrowHalfWidth, arrowCenterX + kArrowHalfWidth};
  const int arrowYPoints[3] = {arrowTopY, arrowTopY + kArrowHeight, arrowTopY + kArrowHeight};
  renderer.fillPolygon(arrowXPoints, arrowYPoints, 3, true);
  renderer.drawLine(0, dividerY, screenW, dividerY, true);
  renderer.displayBuffer(refreshMode);
}

GlobalMenuActivity::ModalResult GlobalMenuActivity::runFirmwareModal(
    GfxRenderer& renderer, MappedInputManager& mappedInput) {
  RuntimeMemory::PsramBuffer snapshot(renderer.getBufferSize(), false);
  if (!snapshot || !renderer.getFrameBuffer()) return ModalResult::Unavailable;
  std::memcpy(snapshot.data(), renderer.getFrameBuffer(), renderer.getBufferSize());

  GlobalMenuActivity menu(renderer, mappedInput, false);
  if (!hasBacklight()) menu.selectedIndex = BUTTON_SHUTDOWN;

  auto redraw = [&] {
    esp_task_wdt_reset();
    menu.renderOverlay(HalDisplay::HALF_REFRESH);
    esp_task_wdt_reset();
  };
  auto restoreApp = [&] {
    std::memcpy(renderer.getFrameBuffer(), snapshot.data(), renderer.getBufferSize());
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  };
  auto requestModalShutdown = [&]() -> bool {
    if (SETTINGS.confirmShutdown && !modalShutdownConfirmed(renderer, mappedInput)) {
      redraw();
      return false;
    }
    requestShutdown();
    return true;
  };

  redraw();

  for (;;) {
    esp_task_wdt_reset();
    delay(10);
    mappedInput.update();

    if (mappedInput.wasTouchHomeButtonPressed() ||
        mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      restoreApp();
      return ModalResult::Dismissed;
    }

    MappedInputManager::TouchPoint point{};
    if (mappedInput.wasTouchTapped(point, renderer)) {
      int panelX, panelY, panelW, panelH;
      menu.getPanelLayout(panelX, panelY, panelW, panelH);
      const bool inside = point.x >= panelX && point.x < panelX + panelW &&
                          point.y >= panelY && point.y < panelY + panelH;
      if (!inside) {
        restoreApp();
        return ModalResult::Dismissed;
      }

      int bx, by, bw, bh;
      if (hasBacklight()) {
        menu.getButtonRect(BUTTON_BACKLIGHT, bx, by, bw, bh);
        if (point.x >= bx && point.x < bx + bw && point.y >= by && point.y < by + bh) {
          menu.selectedIndex = BUTTON_BACKLIGHT;
          menu.applyBacklightLevel(
              point.x < bx + bw / 2 ? SETTINGS.backlightLevel - 1 : SETTINGS.backlightLevel + 1,
              false);
          redraw();
          continue;
        }
      }

      menu.getButtonRect(BUTTON_SHUTDOWN, bx, by, bw, bh);
      if (point.x >= bx && point.x < bx + bw && point.y >= by && point.y < by + bh) {
        menu.selectedIndex = BUTTON_SHUTDOWN;
        if (requestModalShutdown()) return ModalResult::ShutdownRequested;
        continue;
      }
    }

    bool redrawNeeded = false;
    const auto moveNext = [&] {
      menu.selectedIndex = ButtonNavigator::nextIndex(menu.selectedIndex, BUTTON_COUNT);
      redrawNeeded = true;
    };
    const auto movePrevious = [&] {
      menu.selectedIndex = ButtonNavigator::previousIndex(menu.selectedIndex, BUTTON_COUNT);
      redrawNeeded = true;
    };

    if (!hasBacklight()) {
      menu.selectedIndex = BUTTON_SHUTDOWN;
    } else if (menu.selectedIndex == BUTTON_BACKLIGHT) {
      menu.buttonNavigator.onPressAndContinuous(
          {MappedInputManager::Button::Right}, [&] {
            const int before = SETTINGS.backlightLevel;
            menu.applyBacklightLevel(before + 1, false);
            redrawNeeded = redrawNeeded || before != SETTINGS.backlightLevel;
          });
      menu.buttonNavigator.onPressAndContinuous(
          {MappedInputManager::Button::Left}, [&] {
            const int before = SETTINGS.backlightLevel;
            menu.applyBacklightLevel(before - 1, false);
            redrawNeeded = redrawNeeded || before != SETTINGS.backlightLevel;
          });
      menu.buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, moveNext);
      menu.buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, movePrevious);
    } else {
      menu.buttonNavigator.onNext(moveNext);
      menu.buttonNavigator.onPrevious(movePrevious);
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (menu.selectedIndex == BUTTON_BACKLIGHT) {
        menu.applyBacklightLevel(SETTINGS.backlightLevel >= 10 ? 0 : SETTINGS.backlightLevel + 1, false);
        redrawNeeded = true;
      } else if (requestModalShutdown()) {
        return ModalResult::ShutdownRequested;
      }
    }

    if (redrawNeeded) redraw();
  }
}

void GlobalMenuActivity::render(RenderLock&&) {
  renderOverlay(overGrayscaleReader ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
}
