#include "SettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "native/NativeAppHost.h"

void SettingsActivity::onEnter() {
  Activity::onEnter();
  launchAttempted = false;
  launchFailed = false;
  requestUpdate();
}

void SettingsActivity::onExit() {
  Activity::onExit();
  UITheme::getInstance().reload();
}

void SettingsActivity::loop() {
  if (!launchAttempted) {
    launchAttempted = true;
    const esp_err_t result = runNativeApp("/sd/Apps/settings.elf", renderer, mappedInput);
    if (result == ESP_OK) {
      // settings.elf returns both when the user leaves Settings and when it has
      // requested a firmware-owned Settings action. In the latter case
      // runNativeApp() has already queued that action. Do not pop this parent in
      // the same ActivityManager iteration or popActivity() will discard the
      // pending action and unwind to Home.
      return;
    }
    launchFailed = true;
    requestUpdate();
    return;
  }

  if (!launchFailed) {
    // If settings.elf queued a sub-app, this activity was pushed underneath it
    // before we get here and resumes only after the whole Settings handoff chain
    // has unwound. If no action was queued, this simply closes Settings one loop
    // later after the user backed out of settings.elf.
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    finish();
  }
}

bool SettingsActivity::onTouchTap(int16_t x, int16_t y) {
  if (!launchFailed) return true;
  MappedInputManager::Button button = MappedInputManager::Button::Back;
  if (!resolveTouchButtonHint(x, y, button)) return false;
  if (button == MappedInputManager::Button::Back || button == MappedInputManager::Button::Confirm) finish();
  return true;
}

void SettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SETTINGS_TITLE));
  const int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 3;
  if (launchFailed) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, "settings.elf could not be launched");
    renderer.drawCenteredText(SMALL_FONT_ID, y + 36, "Install Settings from the App Store or copy it to /Apps.");
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "OK", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Opening Settings...");
  }
  renderer.displayBuffer(HalDisplay::BALANCED_REFRESH);
}
