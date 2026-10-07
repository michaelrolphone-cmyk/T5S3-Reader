#include "runtime/network/NetworkService.h"
#include "runtime/network/SavedNetworkConnection.h"
#include "WifiCredentialStore.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace {
RuntimeNetwork::ConnectionState connection = RuntimeNetwork::ConnectionState::Disconnected;
bool has_address = false;
uint32_t fake_ms = 0;
std::string active_ssid;
std::string failed_ssid;
std::string successful_ssid;
std::vector<std::string> connect_attempts;

void connectNetwork(const char* ssid, const char*) {
  active_ssid = ssid ? ssid : "";
  connect_attempts.push_back(active_ssid);
  has_address = false;
  if (active_ssid == failed_ssid) {
    connection = RuntimeNetwork::ConnectionState::Failed;
  } else if (!successful_ssid.empty() && active_ssid == successful_ssid) {
    connection = RuntimeNetwork::ConnectionState::Connected;
    has_address = true;
  } else {
    connection = RuntimeNetwork::ConnectionState::Connecting;
  }
}

void disconnectNetwork() {
  connection = RuntimeNetwork::ConnectionState::Disconnected;
  has_address = false;
}
}  // namespace

unsigned long millis() { return fake_ms; }
void delay(uint32_t ms) { fake_ms += ms; }
void esp_task_wdt_reset() {}

namespace RuntimeNetwork {
bool ready() {
  return connection == ConnectionState::Connected && has_address;
}

InterfaceState state() {
  InterfaceState result;
  result.connection = connection;
  result.hasAddress = has_address;
  return result;
}

const WifiControlApi& wifi() {
  static const WifiControlApi api = [] {
    WifiControlApi value{};
    value.apiVersion = WIFI_CONTROL_API_VERSION;
    value.structSize = sizeof(WifiControlApi);
    value.connect = connectNetwork;
    value.disconnect = disconnectNetwork;
    return value;
  }();
  return api;
}
}  // namespace RuntimeNetwork

int main() {
  auto& store = WIFI_STORE;

  // Preferred profile fails; the next saved profile is reachable.
  store.reset({{"unavailable", "p1"}, {"reachable", "p2"}}, "unavailable");
  failed_ssid = "unavailable";
  successful_ssid = "reachable";
  connect_attempts.clear();
  fake_ms = 0;
  connection = RuntimeNetwork::ConnectionState::Disconnected;
  has_address = false;
  assert(RuntimeNetwork::ensureSavedConnection(1000));
  assert((connect_attempts == std::vector<std::string>{"unavailable", "reachable"}));
  assert(store.getLastConnectedSsid() == "reachable");

  // Failed attempts share one timeout and duplicate SSIDs are tried once.
  store.reset({{"slow-a", "p1"}, {"slow-b", "p2"}, {"slow-b", "p3"}}, "slow-a");
  failed_ssid.clear();
  successful_ssid.clear();
  connect_attempts.clear();
  fake_ms = 0;
  connection = RuntimeNetwork::ConnectionState::Disconnected;
  has_address = false;
  assert(!RuntimeNetwork::ensureSavedConnection(100));
  assert((connect_attempts == std::vector<std::string>{"slow-a", "slow-b"}));
  assert(fake_ms == 100);

  // A later call can retry and succeed after a saved network recovers.
  successful_ssid = "slow-b";
  connect_attempts.clear();
  fake_ms = 0;
  connection = RuntimeNetwork::ConnectionState::Disconnected;
  has_address = false;
  assert(RuntimeNetwork::ensureSavedConnection(100));
  assert(store.getLastConnectedSsid() == "slow-b");

  // A stale preferred SSID falls back to the first saved profile, and no
  // connection attempt is made when the store is empty.
  store.reset({{"reachable", "p1"}, {"backup", "p2"}}, "removed-network");
  failed_ssid.clear();
  successful_ssid = "reachable";
  connect_attempts.clear();
  fake_ms = 0;
  connection = RuntimeNetwork::ConnectionState::Disconnected;
  has_address = false;
  assert(RuntimeNetwork::ensureSavedConnection(100));
  assert((connect_attempts == std::vector<std::string>{"reachable"}));

  store.reset({}, "removed-network");
  connect_attempts.clear();
  connection = RuntimeNetwork::ConnectionState::Disconnected;
  has_address = false;
  assert(!RuntimeNetwork::ensureSavedConnection(100));
  assert(connect_attempts.empty());
  return 0;
}
