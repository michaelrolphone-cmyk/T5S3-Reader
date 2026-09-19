#include "OtaUpdateActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <string>

#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "runtime/network/NetworkService.h"
#include "native/InstalledAppPath.h"
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

  RuntimeNetwork::wifi().stationMode();
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void OtaUpdateActivity::onExit() {
  Activity::onExit();
  RuntimeNetwork::shutdown();
}

void OtaUpdateActivity::loop() {
  if (state == LAUNCH_PENDING && !launchAttempted) {
    launchAttempted = true;
    // The package manager installs into /Apps/<package-id>/<artifact>. The
    // old hard-coded loose-file path failed even with a valid OTA package.
    std::string updaterPath;
    if (!resolveInstalledAppPath("ota_update.elf", updaterPath)) {
      LOG_ERR("OTA", "Firmware Update app is missing, incompatible or failed package verification");
      state = LAUNCH_FAILED;
      requestUpdate();
      return;
    }
    const esp_err_t result = runNativeApp(updaterPath.c_str(), renderer, mappedInput);
    if (result == ESP_OK) {
      finish();
      return;
    }
    LOG_ERR("OTA", "Firmware Update ELF launch failed: 0x%lX", static_cast<unsigned long>(result));
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
                              "Install Firmware Update from the App Store or check its package.");
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "OK", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == LAUNCH_PENDING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Opening Firmware Update...");
  }

  renderer.displayBuffer(HalDisplay::BALANCED_REFRESH);
}
