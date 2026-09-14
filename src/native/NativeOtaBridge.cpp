#include <T5AppApi.h>
#include <T5OtaApi.h>

#include <Arduino.h>

#include <cstring>

#include "network/OtaUpdater.h"

namespace {

OtaUpdater updater;

bool active() { return t5_app_get_api(T5_APP_ABI_VERSION) != nullptr; }

t5_ota_result_t mapResult(OtaUpdater::OtaUpdaterError result) {
  switch (result) {
    case OtaUpdater::OK:
      return T5_OTA_OK;
    case OtaUpdater::NO_UPDATE:
      return T5_OTA_NO_UPDATE;
    case OtaUpdater::HTTP_ERROR:
      return T5_OTA_HTTP_ERROR;
    case OtaUpdater::JSON_PARSE_ERROR:
      return T5_OTA_JSON_PARSE_ERROR;
    case OtaUpdater::UPDATE_OLDER_ERROR:
      return T5_OTA_UPDATE_OLDER_ERROR;
    case OtaUpdater::INTERNAL_UPDATE_ERROR:
      return T5_OTA_INTERNAL_UPDATE_ERROR;
    case OtaUpdater::OOM_ERROR:
      return T5_OTA_OOM_ERROR;
  }
  return T5_OTA_UNAVAILABLE;
}

t5_ota_result_t checkForUpdate() {
  if (!active()) return T5_OTA_UNAVAILABLE;
  return mapResult(updater.checkForUpdate());
}

bool isUpdateNewer() { return active() && updater.isUpdateNewer(); }

bool latestVersion(char* buffer, size_t bufferSize) {
  if (!active() || !buffer || bufferSize == 0) return false;
  const auto& value = updater.getLatestVersion();
  std::strncpy(buffer, value.c_str(), bufferSize - 1);
  buffer[bufferSize - 1] = '\0';
  return true;
}

size_t processedSize() { return active() ? updater.getProcessedSize() : 0; }
size_t totalSize() { return active() ? updater.getTotalSize() : 0; }

t5_ota_result_t installUpdate(t5_ota_progress_callback_t callback, void* ctx) {
  if (!active()) return T5_OTA_UNAVAILABLE;
  return mapResult(updater.installUpdate(callback, ctx));
}

void restartAfterUpdate() {
  if (!active()) return;
  ESP.restart();
}

const t5_ota_api_v1 api = {
    T5_OTA_API_VERSION,
    sizeof(t5_ota_api_v1),
    checkForUpdate,
    isUpdateNewer,
    latestVersion,
    processedSize,
    totalSize,
    installUpdate,
    restartAfterUpdate,
};

}  // namespace

extern "C" const t5_ota_api_v1* t5_ota_get_api(uint32_t version) {
  return version == T5_OTA_API_VERSION && active() ? &api : nullptr;
}
