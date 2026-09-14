#include "OtaUpdateActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "native/NativeAppHost.h"

void OtaUpdateActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    finish();
    return;
  }

  state = LAUNCH_PENDING;
  launchAttempted = false;
  requestUpdate();
}

void OtaUpdateActivity::onEnter() {
  Activity::onEnter();
  state = WIFI_SELECTION;
  launchAttempted = false;

  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void OtaUpdateActivity::onExit() {
  Activity::onExit();
  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);
}

void OtaUpdateActivity::loop() {
  if (state == LAUNCH_PENDING && !launchAttempted) {
    launchAttempted = true;
    const esp_err_t result = runNativeApp("/sd/Apps/ota_update.elf", renderer, mappedInput);
    if (result == ESP_OK) {
      finish();
      return;
    }
    state = LAUNCH_FAILED;
    requestUpdate();
    return;
  }

  if (state == LAUNCH_FAILED &&
      (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
       mappedInput.wasPressed(MappedInputManager::Button::Confirm))) {
    finish();
  }
}

bool OtaUpdateActivity::onTouchTap(int16_t x, int16_t y) {
  if (state != LAUNCH_FAILED) return true;
  MappedInputManager::Button button = MappedInputManager::Button::Back;
  if (!resolveTouchButtonHint(x, y, button)) return false;
  if (button == MappedInputManager::Button::Back || button == MappedInputManager::Button::Confirm) finish();
  return true;
}

void OtaUpdateActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_UPDATE));
  const int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 3;

  if (state == LAUNCH_FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, "ota_update.elf could not be launched");
    renderer.drawCenteredText(SMALL_FONT_ID, y + 36,
                              "Install Firmware Update from the App Store or copy it to /Apps.");
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "OK", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == LAUNCH_PENDING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Opening Firmware Update...");
  }

  renderer.displayBuffer(HalDisplay::BALANCED_REFRESH);
}
