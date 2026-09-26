#include "NetworkService.h"

#include "providers/network/Esp32NetworkProvider.h"

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
void shutdown() { interfaceApi(INTERFACE_API_VERSION)->shutdown(); }
const WifiControlApi& wifi() { return *wifiControlApi(WIFI_CONTROL_API_VERSION); }
}  // namespace RuntimeNetwork
