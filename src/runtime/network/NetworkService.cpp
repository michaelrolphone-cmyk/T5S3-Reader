#include "NetworkService.h"

#include "providers/network/Esp32NetworkProvider.h"

#include <Arduino.h>
#include <esp_task_wdt.h>

#include "WifiCredentialStore.h"

namespace RuntimeNetwork {
const InterfaceApi* interfaceApi(uint32_t version) {
  const auto& api = Esp32NetworkProvider::interfaceApi();
  return version == api.apiVersion && api.structSize >= sizeof(InterfaceApi) ? &api : nullptr;
}

const WifiControlApi* wifiControlApi(uint32_t version) {
  const auto& api = Esp32NetworkProvider::wifiControlApi();
  return version == api.apiVersion && api.structSize >= sizeof(WifiControlApi) ? &api : nullptr;
}

InterfaceState state() { return interfaceApi(INTERFACE_API_VERSION)->state(); }
bool connected() { return state().connection == ConnectionState::Connected; }
bool ready() {
  const auto current = state();
  return current.connection == ConnectionState::Connected && current.hasAddress;
}
bool ensureConnected(uint32_t timeoutMs) {
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
    if (ready()) return true;
    esp_task_wdt_reset();
    delay(25);
  }
  return ready();
}
void shutdown() { interfaceApi(INTERFACE_API_VERSION)->shutdown(); }
const WifiControlApi& wifi() { return *wifiControlApi(WIFI_CONTROL_API_VERSION); }
}  // namespace RuntimeNetwork
