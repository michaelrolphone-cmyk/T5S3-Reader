#include "SdFirmwareUpdateActivity.h"

#include <Arduino.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_ota_ops.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <string_view>
#include <utility>

#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/RequiredAppActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "native/InstalledAppPath.h"
#include "native/NativeAppHost.h"
#include "network/FirmwareFlasher.h"

namespace {
std::string recoveryDisplayName(const std::string& entry) {
  if (!entry.empty() && entry.back() == '/') return entry.substr(0, entry.size() - 1);
  return entry;
}
}  // namespace

void SdFirmwareUpdateActivity::onEnter() {
  Activity::onEnter();
  launchFailed = false;
  installRetry = false;
  browserPending = false;
  browserFinished = false;
  errorMessage.clear();

  if (!recoveryMode) {
    state = State::OPENING_BROWSER;
    launchFileBrowser();
    return;
  }

  state = State::PICKING;
  pickerPath = "/";
  pickerIndex = 0;
  loadRecoveryEntries();
  requestUpdate();
}

void SdFirmwareUpdateActivity::launchFileBrowser() {
  if (recoveryMode) return;

  std::string updaterPath;
  if (!resolveInstalledAppPath("sd_firmware_update.elf", updaterPath)) {
    startActivityForResult(
        std::make_unique<RequiredAppActivity>(
            renderer, mappedInput, "sd_firmware_update.elf", "SD Firmware Update"),
        [this](const ActivityResult& result) {
          if (result.isCancelled) {
            launchFailed = true;
          } else {
            installRetry = true;
          }
          requestUpdate();
        });
    return;
  }

  if (!resolveInstalledAppPath("file_browser.elf", browserPath)) {
    startActivityForResult(
        std::make_unique<RequiredAppActivity>(
            renderer, mappedInput, "file_browser.elf", "File Browser"),
        [this](const ActivityResult& result) {
          if (result.isCancelled) {
            launchFailed = true;
          } else {
            installRetry = true;
          }
          requestUpdate();
        });
    return;
  }

  browserPending = true;
  requestUpdate();
}

void SdFirmwareUpdateActivity::runFileBrowser() {
  if (browserPath.empty()) {
    launchFailed = true;
    requestUpdate();
    return;
  }

  const esp_err_t result = runNativeApp(browserPath.c_str(), renderer, mappedInput);
  if (result != ESP_OK) {
    launchFailed = true;
    requestUpdate();
    return;
  }

  // File Browser may have queued a file-association handoff to the SD Firmware
  // Update ELF. Do not finish in this same ActivityManager iteration: the
  // pending child must be pushed first. We finish when this wrapper resumes.
  browserFinished = true;
  requestUpdate();
}

void SdFirmwareUpdateActivity::loadRecoveryEntries() {
  pickerEntries.clear();

  HalFile root = Storage.open(pickerPath.c_str());
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    pickerPath = "/";
    root = Storage.open("/");
  }
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    pickerIndex = 0;
    return;
  }

  root.rewindDirectory();
  char name[500];
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    if (name[0] == '.' || std::strcmp(name, "System Volume Information") == 0) continue;
    if (file.isDirectory()) {
      pickerEntries.emplace_back(std::string(name) + "/");
    } else if (FsHelpers::checkFileExtension(std::string_view{name}, ".bin")) {
      pickerEntries.emplace_back(name);
    }
  }
  root.close();

  FsHelpers::sortFileList(pickerEntries);
  if (pickerEntries.empty()) pickerIndex = 0;
  else if (pickerIndex >= pickerEntries.size()) pickerIndex = pickerEntries.size() - 1;
}

void SdFirmwareUpdateActivity::openRecoveryEntry() {
  if (pickerEntries.empty() || pickerIndex >= pickerEntries.size()) return;

  const std::string entry = pickerEntries[pickerIndex];
  if (!entry.empty() && entry.back() == '/') {
    if (pickerPath != "/") pickerPath += "/";
    pickerPath += entry.substr(0, entry.size() - 1);
    pickerIndex = 0;
    loadRecoveryEntries();
    requestUpdate();
    return;
  }

  firmwarePath = pickerPath == "/" ? "/" + entry : pickerPath + "/" + entry;
  state = State::VALIDATING;
  requestUpdateAndWait();
  if (!validateFirmware()) {
    state = State::FAILED;
    requestUpdate();
    return;
  }
  promptConfirmation();
}

void SdFirmwareUpdateActivity::goRecoveryUp() {
  if (pickerPath == "/") return;
  const std::string oldPath = pickerPath;
  const auto slash = pickerPath.find_last_of('/');
  pickerPath = slash == 0 ? "/" : pickerPath.substr(0, slash);
  loadRecoveryEntries();

  const auto oldSlash = oldPath.find_last_of('/');
  const std::string child = oldPath.substr(oldSlash + 1) + "/";
  const auto it = std::find(pickerEntries.begin(), pickerEntries.end(), child);
  pickerIndex = it == pickerEntries.end() ? 0 : static_cast<size_t>(std::distance(pickerEntries.begin(), it));
  requestUpdate();
}

bool SdFirmwareUpdateActivity::validateFirmware() {
  HalFile file;
  if (!Storage.openFileForRead("FW", firmwarePath.c_str(), file) || !file) {
    errorMessage = tr(STR_FIRMWARE_FILE_OPEN_FAILED);
    return false;
  }
  firmwareSize = file.fileSize();
  file.close();

  const esp_partition_t* dest = esp_ota_get_next_update_partition(nullptr);
  if (!dest) {
    errorMessage = tr(STR_INVALID_FIRMWARE);
    return false;
  }
  if (firmwareSize > dest->size) {
    errorMessage = tr(STR_FIRMWARE_TOO_LARGE);
    return false;
  }

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
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(
          renderer, mappedInput, tr(STR_FIRMWARE_UPDATE_PROMPT), body),
      [this](const ActivityResult& result) { onConfirmationResult(result); });
}

void SdFirmwareUpdateActivity::onConfirmationResult(const ActivityResult& result) {
  if (result.isCancelled) {
    state = State::PICKING;
    requestUpdate();
    return;
  }
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
  if (result != firmware_flash::Result::OK) {
    errorMessage = tr(STR_FIRMWARE_WRITE_FAILED);
    state = State::FAILED;
    requestUpdate();
    return;
  }
  state = State::SUCCESS;
  requestUpdateAndWait();
  delay(1500);
  ESP.restart();
}

void SdFirmwareUpdateActivity::loop() {
  if (!recoveryMode) {
    if (installRetry) {
      installRetry = false;
      launchFileBrowser();
      return;
    }
    if (browserPending) {
      browserPending = false;
      runFileBrowser();
      return;
    }
    if (browserFinished) {
      finish();
      return;
    }
    if (launchFailed &&
        (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
         mappedInput.wasReleased(MappedInputManager::Button::Confirm))) {
      finish();
    }
    return;
  }

  if (state == State::PICKING) {
    const int count = static_cast<int>(pickerEntries.size());
    if (count > 0) {
      buttonNavigator.onNextRelease([this, count] {
        pickerIndex = static_cast<size_t>(ButtonNavigator::nextIndex(static_cast<int>(pickerIndex), count));
        requestUpdate();
      });
      buttonNavigator.onPreviousRelease([this, count] {
        pickerIndex = static_cast<size_t>(ButtonNavigator::previousIndex(static_cast<int>(pickerIndex), count));
        requestUpdate();
      });
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      openRecoveryEntry();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      goRecoveryUp();
      return;
    }
    return;
  }

  if (state != State::FAILED) return;
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    state = State::PICKING;
    requestUpdate();
  }
}

bool SdFirmwareUpdateActivity::onTouchTap(int16_t, int16_t y) {
  if (!recoveryMode) {
    if (!launchFailed) return true;
    finish();
    return true;
  }

  if (state == State::FAILED) {
    state = State::PICKING;
    requestUpdate();
    return true;
  }
  if (state != State::PICKING || pickerEntries.empty()) return false;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageHeight = renderer.getScreenHeight();
  const int pathLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int contentTop = metrics.topPadding + metrics.headerHeight +
                         metrics.verticalSpacing + pathLineHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int rowHeight = metrics.listRowHeight;
  if (rowHeight <= 0 || y < contentTop || y >= contentTop + contentHeight) return false;

  const int pageItems = std::max(1, contentHeight / rowHeight);
  const size_t pageStart = (pickerIndex / static_cast<size_t>(pageItems)) * static_cast<size_t>(pageItems);
  const int row = (y - contentTop) / rowHeight;
  const size_t touched = pageStart + static_cast<size_t>(row);
  if (row < 0 || row >= pageItems || touched >= pickerEntries.size()) return false;

  pickerIndex = touched;
  openRecoveryEntry();
  return true;
}

void SdFirmwareUpdateActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();

  if (!recoveryMode) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   tr(STR_SD_FIRMWARE_UPDATE));
    const int top = pageHeight / 2;
    if (launchFailed) {
      renderer.drawCenteredText(UI_10_FONT_ID, top, "File Browser could not be launched");
      renderer.drawCenteredText(SMALL_FONT_ID, top + 36,
                                "Install File Browser and SD Firmware Update, then retry.");
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "OK", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, top, "Opening File Browser...");
      renderer.drawCenteredText(SMALL_FONT_ID, top + 36, "Select a .bin file to update firmware.");
    }
    renderer.displayBuffer(HalDisplay::BALANCED_REFRESH);
    return;
  }

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 tr(STR_RECOVERY_MODE));

  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - lineHeight) / 2;

  if (state == State::PICKING) {
    const int pathLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const int pathY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, pathY, pickerPath.c_str());

    const int contentTop = pathY + pathLineHeight + metrics.verticalSpacing;
    const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
    if (pickerEntries.empty()) {
      renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_BIN_FILES));
    } else {
      GUI.drawList(
          renderer, Rect{0, contentTop, pageWidth, contentHeight},
          static_cast<int>(pickerEntries.size()), static_cast<int>(pickerIndex),
          [this](int index) { return recoveryDisplayName(pickerEntries[index]); },
          nullptr,
          [this](int index) { return UITheme::getFileIcon(pickerEntries[index]); },
          [this](int index) {
            return !pickerEntries[index].empty() && pickerEntries[index].back() == '/'
                       ? std::string()
                       : std::string(".bin");
          },
          false, TextRole::UserContent);
    }
    const auto labels = mappedInput.mapLabels(
        pickerPath == "/" ? "" : tr(STR_BACK),
        pickerEntries.empty() ? "" : tr(STR_SELECT),
        pickerEntries.empty() ? "" : tr(STR_DIR_UP),
        pickerEntries.empty() ? "" : tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == State::VALIDATING) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_VALIDATING_FIRMWARE));
  } else if (state == State::UPDATING) {
    const unsigned int pct = firmwareSize > 0 ? static_cast<unsigned int>((writtenBytes * 100) / firmwareSize) : 0;
    if (pct == lastRenderedPercent) return;
    lastRenderedPercent = pct;
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATING), true, EpdFontFamily::BOLD);
    GUI.drawProgressBar(renderer,
                        Rect{metrics.contentSidePadding, top + lineHeight + metrics.verticalSpacing,
                             pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
                        static_cast<int>(pct), 100);
  } else if (state == State::SUCCESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_COMPLETE), true, EpdFontFamily::BOLD);
  } else if (state == State::FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_FAILED), true, EpdFontFamily::BOLD);
    if (!errorMessage.empty()) {
      renderer.drawCenteredText(UI_10_FONT_ID, top + lineHeight + metrics.verticalSpacing, errorMessage.c_str());
    }
  }

  renderer.displayBuffer();
}
