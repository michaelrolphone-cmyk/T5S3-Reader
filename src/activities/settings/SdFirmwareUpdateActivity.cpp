#include "SdFirmwareUpdateActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_ota_ops.h>

#include "MappedInputManager.h"
#include "activities/home/FileBrowserActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "native/NativeAppHost.h"
#include "native/NativeSdFirmwareBridge.h"
#include "network/FirmwareFlasher.h"

void SdFirmwareUpdateActivity::onEnter() {
  Activity::onEnter();
  state = State::PICKING;
  launchFailed = false;
  launchPicker();
}

void SdFirmwareUpdateActivity::launchPicker() {
  startActivityForResult(
      std::make_unique<FileBrowserActivity>(renderer, mappedInput, "/", FileBrowserActivity::Mode::PickFirmware),
      [this](const ActivityResult& result) { onPickerResult(result); });
}

void SdFirmwareUpdateActivity::onPickerResult(const ActivityResult& result) {
  if (result.isCancelled) {
    if (recoveryMode) { launchPicker(); return; }
    finish();
    return;
  }
  const auto* path = std::get_if<FilePathResult>(&result.data);
  if (!path) { finish(); return; }
  firmwarePath = path->path;

  if (!recoveryMode) {
    NativeSdFirmwareBridge::setSelectedPath(firmwarePath.c_str());
    const esp_err_t rc = runNativeApp("/sd/Apps/sd_firmware_update.elf", renderer, mappedInput);
    NativeSdFirmwareBridge::clearSelectedPath();
    if (rc == ESP_OK) { finish(); return; }
    launchFailed = true;
    state = State::FAILED;
    requestUpdate();
    return;
  }

  { RenderLock lock(*this); state = State::VALIDATING; }
  requestUpdateAndWait();
  if (!validateFirmware()) { state = State::FAILED; requestUpdate(); return; }
  promptConfirmation();
}

bool SdFirmwareUpdateActivity::validateFirmware() {
  HalFile file;
  if (!Storage.openFileForRead("FW", firmwarePath.c_str(), file) || !file) { errorMessage = tr(STR_FIRMWARE_FILE_OPEN_FAILED); return false; }
  firmwareSize = file.fileSize();
  file.close();
  const esp_partition_t* dest = esp_ota_get_next_update_partition(nullptr);
  if (!dest) { errorMessage = tr(STR_INVALID_FIRMWARE); return false; }
  if (firmwareSize > dest->size) { errorMessage = tr(STR_FIRMWARE_TOO_LARGE); return false; }
  const auto vr = firmware_flash::validateImageFile(firmwarePath.c_str(), dest->size);
  if (vr != firmware_flash::Result::OK) {
    errorMessage = vr == firmware_flash::Result::TOO_LARGE ? tr(STR_FIRMWARE_TOO_LARGE)
                 : vr == firmware_flash::Result::TOO_SMALL ? tr(STR_FIRMWARE_TOO_SMALL)
                 : tr(STR_INVALID_FIRMWARE);
    return false;
  }
  return true;
}

void SdFirmwareUpdateActivity::promptConfirmation() {
  state = State::CONFIRMING;
  std::string body = firmwarePath;
  const auto pos = body.find_last_of('/');
  if (pos != std::string::npos) body = body.substr(pos + 1);
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_FIRMWARE_UPDATE_PROMPT), body),
                         [this](const ActivityResult& result) { onConfirmationResult(result); });
}

void SdFirmwareUpdateActivity::onConfirmationResult(const ActivityResult& result) {
  if (result.isCancelled) { launchPicker(); return; }
  state = State::UPDATING;
  writtenBytes = 0;
  lastRenderedPercent = 101;
  requestUpdateAndWait();
  performUpdate();
}

void SdFirmwareUpdateActivity::performUpdate() {
  auto progressCb = +[](size_t written, size_t total, void* ctx) {
    auto* self = static_cast<SdFirmwareUpdateActivity*>(ctx);
    self->writtenBytes = written;
    self->firmwareSize = total;
    self->requestUpdate(true);
  };
  const auto result = firmware_flash::flashFromSdPath(firmwarePath.c_str(), progressCb, this);
  if (result != firmware_flash::Result::OK) { errorMessage = tr(STR_FIRMWARE_WRITE_FAILED); state = State::FAILED; requestUpdate(); return; }
  state = State::SUCCESS;
  requestUpdateAndWait();
  delay(1500);
  ESP.restart();
}

void SdFirmwareUpdateActivity::loop() {
  if (state != State::FAILED) return;
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (recoveryMode) { state = State::PICKING; launchPicker(); } else finish();
  }
}

bool SdFirmwareUpdateActivity::onTouchTap(int16_t, int16_t) {
  if (state != State::FAILED) return false;
  if (recoveryMode) { state = State::PICKING; launchPicker(); } else finish();
  return true;
}

void SdFirmwareUpdateActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, recoveryMode ? tr(STR_RECOVERY_MODE) : tr(STR_SD_FIRMWARE_UPDATE));
  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - lineHeight) / 2;
  if (!recoveryMode && launchFailed) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, "sd_firmware_update.elf could not be launched");
    renderer.drawCenteredText(SMALL_FONT_ID, top + 36, "Install SD Firmware Update from the App Store or copy it to /Apps.");
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "OK", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == State::VALIDATING) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_VALIDATING_FIRMWARE));
  } else if (state == State::UPDATING) {
    const unsigned int pct = firmwareSize > 0 ? static_cast<unsigned int>((writtenBytes * 100) / firmwareSize) : 0;
    if (pct == lastRenderedPercent) return;
    lastRenderedPercent = pct;
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATING), true, EpdFontFamily::BOLD);
    GUI.drawProgressBar(renderer, Rect{metrics.contentSidePadding, top + lineHeight + metrics.verticalSpacing,
                        pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight}, static_cast<int>(pct), 100);
  } else if (state == State::SUCCESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_COMPLETE), true, EpdFontFamily::BOLD);
  } else if (state == State::FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_FAILED), true, EpdFontFamily::BOLD);
    if (!errorMessage.empty()) renderer.drawCenteredText(UI_10_FONT_ID, top + lineHeight + metrics.verticalSpacing, errorMessage.c_str());
  } else if (recoveryMode) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_RECOVERY_MODE_HINT));
  }
  renderer.displayBuffer();
}
