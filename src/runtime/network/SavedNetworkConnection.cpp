#include "SavedNetworkConnection.h"

#include <Arduino.h>
#include <esp_task_wdt.h>

#include "NetworkService.h"
#include "WifiCredentialStore.h"

namespace RuntimeNetwork {

bool ensureSavedConnection(uint32_t timeoutMs) {
  if (ready()) return true;

  WIFI_STORE.loadFromFile();
  const WifiCredential* credential = nullptr;
  const std::string last = WIFI_STORE.getLastConnectedSsid();
  if (!last.empty()) credential = WIFI_STORE.findCredential(last);
  if (!credential) {
    const auto& credentials = WIFI_STORE.getCredentials();
    if (!credentials.empty()) credential = &credentials.front();
  }
  if (!credential || credential->ssid.empty()) return false;

  wifi().connect(credential->ssid.c_str(),
                 credential->password.empty() ? nullptr : credential->password.c_str());

  const uint32_t started = ::millis();
  const uint32_t budget = timeoutMs ? timeoutMs : 15000u;
  while (::millis() - started < budget) {
    if (ready()) {
      WIFI_STORE.setLastConnectedSsid(credential->ssid);
      return true;
    }
    esp_task_wdt_reset();
    delay(25);
  }
  if (ready()) {
    WIFI_STORE.setLastConnectedSsid(credential->ssid);
    return true;
  }
  return false;
}

}  // namespace RuntimeNetwork
