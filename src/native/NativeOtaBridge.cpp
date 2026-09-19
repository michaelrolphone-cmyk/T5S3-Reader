#include <T5AppApi.h>
#include <T5OtaApi.h>

#include <Arduino.h>
#include <Logging.h>
#include <esp_task_wdt.h>

#include <cstring>
#include <string>

#include "WifiCredentialStore.h"
#include "network/OtaUpdater.h"
#include "runtime/network/NetworkService.h"

namespace {

OtaUpdater updater;
constexpr uint32_t kWifiConnectTimeoutMs = 15000;

bool active() { return t5_app_get_api(T5_APP_ABI_VERSION) != nullptr; }

// Settings connects Wi-Fi before launching this ELF, but a user can also
// launch the updater directly from Apps or Home. Never invoke ESP-IDF HTTP
// without a connected interface and usable IP: an uninitialized lwIP stack
// can assert in tcpip_send_msg_wait_sem with "Invalid mbox".
bool ensureOtaNetworkReady() {
  if (RuntimeNetwork::ready()) return true;

  // The Wi-Fi picker can report WL_CONNECTED before DHCP assigns an IP. Keep
  // its selected network, including unsaved credentials, rather than forcibly
  // reconnecting a different saved SSID while address assignment is underway.
  if (RuntimeNetwork::state().connection == RuntimeNetwork::ConnectionState::Connected) {
    LOG_INF("OTA", "Wi-Fi associated; waiting for an IP address");
    const uint32_t started = millis();
    while (millis() - started < kWifiConnectTimeoutMs) {
      esp_task_wdt_reset();
      const auto state = RuntimeNetwork::state();
      if (state.connection == RuntimeNetwork::ConnectionState::Connected && state.hasAddress)
        return true;
      if (state.connection != RuntimeNetwork::ConnectionState::Connected) break;
      delay(100);
    }
    LOG_ERR("OTA", "Selected Wi-Fi did not obtain an IP address; skipping HTTP request");
    return false;
  }

  WIFI_STORE.loadFromFile();
  const WifiCredential* credential = nullptr;
  const std::string last = WIFI_STORE.getLastConnectedSsid();
  if (!last.empty()) credential = WIFI_STORE.findCredential(last);
  if (!credential) {
    const auto& saved = WIFI_STORE.getCredentials();
    if (!saved.empty()) credential = &saved.front();
  }
  if (!credential || credential->ssid.empty()) {
    LOG_ERR("OTA", "No active network or saved Wi-Fi credentials; skipping HTTP request");
    return false;
  }

  // Copy credentials before modifying the store on successful connection.
  const std::string ssid = credential->ssid;
  const std::string password = credential->password;
  LOG_INF("OTA", "No ready network; connecting saved Wi-Fi before update request");
  RuntimeNetwork::wifi().connect(ssid.c_str(), password.empty() ? nullptr : password.c_str());

  const uint32_t started = millis();
  while (millis() - started < kWifiConnectTimeoutMs) {
    esp_task_wdt_reset();
    const auto state = RuntimeNetwork::state();
    if (state.connection == RuntimeNetwork::ConnectionState::Connected && state.hasAddress) {
      WIFI_STORE.setLastConnectedSsid(ssid);
      LOG_INF("OTA", "Network ready for firmware update");
      return true;
    }
    if (state.connection == RuntimeNetwork::ConnectionState::Failed ||
        state.connection == RuntimeNetwork::ConnectionState::NetworkNotFound) {
      LOG_ERR("OTA", "Saved Wi-Fi connection failed before IP assignment");
      return false;
    }
    delay(100);
  }
  LOG_ERR("OTA", "Timed out waiting for Wi-Fi and an IP address");
  return RuntimeNetwork::ready();
}

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
  if (!active() || !ensureOtaNetworkReady()) return T5_OTA_UNAVAILABLE;
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
  if (!active() || !ensureOtaNetworkReady()) return T5_OTA_UNAVAILABLE;
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
