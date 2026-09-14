#include "StatusBarSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "native/NativeAppHost.h"

void StatusBarSettingsActivity::onEnter() {
  Activity::onEnter();
  launchAttempted = false;
  launchFailed = false;
  requestUpdate();
}

void StatusBarSettingsActivity::loop() {
  if (!launchAttempted) {
    launchAttempted = true;
    const esp_err_t result = runNativeApp("/sd/Apps/status_bar_settings.elf", renderer, mappedInput);
    if (result == ESP_OK) {
      finish();
      return;
    }
    launchFailed = true;
    requestUpdate();
    return;
  }

  if (launchFailed && (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
                       mappedInput.wasPressed(MappedInputManager::Button::Confirm))) {
    finish();
  }
}

bool StatusBarSettingsActivity::onTouchTap(int16_t x, int16_t y) {
  if (!launchFailed) return true;
  MappedInputManager::Button button = MappedInputManager::Button::Back;
  if (!resolveTouchButtonHint(x, y, button)) return false;
  if (button == MappedInputManager::Button::Back || button == MappedInputManager::Button::Confirm) finish();
  return true;
}

void StatusBarSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Customize Status Bar");
  const int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 3;
  if (launchFailed) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, "status_bar_settings.elf could not be launched");
    renderer.drawCenteredText(SMALL_FONT_ID, y + 36, "Install Customize Status Bar from the App Store or copy it to /Apps.");
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "OK", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Opening Customize Status Bar...");
  }
  renderer.displayBuffer(HalDisplay::BALANCED_REFRESH);
}
