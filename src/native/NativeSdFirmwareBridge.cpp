#include "NativeSdFirmwareBridge.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <T5AppApi.h>
#include <T5SdFirmwareApi.h>
#include <esp_ota_ops.h>

#include <cstring>
#include <string>

#include "network/FirmwareFlasher.h"

namespace {
std::string selectedPath;
size_t selectedSize = 0;
size_t writtenSize = 0;

bool active() { return t5_app_get_api(T5_APP_ABI_VERSION) != nullptr; }

bool copySelectedPath(char* buffer, size_t capacity) {
  if (!active() || !buffer || capacity == 0 || selectedPath.empty()) return false;
  std::strncpy(buffer, selectedPath.c_str(), capacity - 1);
  buffer[capacity - 1] = '\0';
  return true;
}

size_t imageSize() { return active() ? selectedSize : 0; }
size_t written() { return active() ? writtenSize : 0; }

t5_sd_firmware_result_t mapResult(firmware_flash::Result result) {
  using R = firmware_flash::Result;
  switch (result) {
    case R::OK: return T5_SD_FIRMWARE_OK;
    case R::OPEN_FAIL: return T5_SD_FIRMWARE_FILE_OPEN_FAILED;
    case R::TOO_LARGE: return T5_SD_FIRMWARE_TOO_LARGE;
    case R::TOO_SMALL: return T5_SD_FIRMWARE_TOO_SMALL;
    case R::WRITE_FAIL:
    case R::ERASE_FAIL:
    case R::OTADATA_FAIL: return T5_SD_FIRMWARE_WRITE_FAILED;
    default: return T5_SD_FIRMWARE_INVALID;
  }
}

t5_sd_firmware_result_t validateSelected() {
  if (!active() || selectedPath.empty()) return T5_SD_FIRMWARE_UNAVAILABLE;

  HalFile file;
  if (!Storage.openFileForRead("FW", selectedPath.c_str(), file) || !file) return T5_SD_FIRMWARE_FILE_OPEN_FAILED;
  selectedSize = file.fileSize();
  file.close();

  const esp_partition_t* dest = esp_ota_get_next_update_partition(nullptr);
  if (!dest) return T5_SD_FIRMWARE_INVALID;
  if (selectedSize > dest->size) return T5_SD_FIRMWARE_TOO_LARGE;
  return mapResult(firmware_flash::validateImageFile(selectedPath.c_str(), dest->size));
}

struct ProgressContext {
  t5_sd_firmware_progress_callback_t callback;
  void* ctx;
};

void progressThunk(size_t written, size_t total, void* raw) {
  writtenSize = written;
  selectedSize = total;
  auto* progress = static_cast<ProgressContext*>(raw);
  if (progress && progress->callback) progress->callback(progress->ctx);
}

t5_sd_firmware_result_t installSelected(t5_sd_firmware_progress_callback_t callback, void* ctx) {
  if (!active() || selectedPath.empty()) return T5_SD_FIRMWARE_UNAVAILABLE;
  writtenSize = 0;
  ProgressContext progress{callback, ctx};
  return mapResult(firmware_flash::flashFromSdPath(selectedPath.c_str(), progressThunk, &progress));
}

void restartAfterUpdate() {
  if (active()) ESP.restart();
}

const t5_sd_firmware_api_v1 api = {
    T5_SD_FIRMWARE_API_VERSION,
    sizeof(t5_sd_firmware_api_v1),
    copySelectedPath,
    imageSize,
    written,
    validateSelected,
    installSelected,
    restartAfterUpdate,
};
}  // namespace

namespace NativeSdFirmwareBridge {
void setSelectedPath(const char* path) {
  selectedPath = path ? path : "";
  selectedSize = 0;
  writtenSize = 0;
}
void clearSelectedPath() {
  selectedPath.clear();
  selectedSize = 0;
  writtenSize = 0;
}
}  // namespace NativeSdFirmwareBridge

extern "C" const t5_sd_firmware_api_v1* t5_sd_firmware_get_api(uint32_t version) {
  return version == T5_SD_FIRMWARE_API_VERSION && active() ? &api : nullptr;
}
