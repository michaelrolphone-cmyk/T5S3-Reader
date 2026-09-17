#include "runtime/network/NetworkService.h"
#include <WiFi.h>
#include <cassert>
#include <cstring>
#include <iostream>

FakeWiFi WiFi;
std::vector<std::string> calls;

int main() {
  using namespace RuntimeNetwork;
  assert(interfaceApi(0) == nullptr && interfaceApi(2) == nullptr);
  assert(wifiControlApi(0) == nullptr && wifiControlApi(2) == nullptr);
  assert(interfaceApi(1)->structSize == sizeof(InterfaceApi));
  assert(wifiControlApi(1)->structSize == sizeof(WifiControlApi));
  assert(!connected() && !ready());
  WiFi.linkStatus = WL_CONNECTED;
  assert(connected() && !ready()); // Association is not an assigned IP address.
  WiFi.ip = IPAddress(10,20,30,40);
  assert(ready() && std::strcmp(state().address, "10.20.30.40") == 0);
  WiFi.linkStatus = WL_DISCONNECTED;
  assert(!ready() && !state().hasAddress); // Ignore a stale DHCP address after loss.
  WiFi.linkStatus = WL_NO_SSID_AVAIL;
  assert(state().connection == ConnectionState::NetworkNotFound);
  WiFi.linkStatus = WL_CONNECT_FAILED;
  assert(state().connection == ConnectionState::Failed);
  WiFi.linkStatus = WL_IDLE_STATUS;
  assert(state().connection == ConnectionState::Connecting);

  assert(wifi().startScan(true, 1000) == SCAN_RUNNING);
  assert((calls == std::vector<std::string>{"scan.clear", "persistent:off", "mode:0", "delay:150", "mode:1",
          "sleep:off", "disconnect:00", "delay:150", "scan:111:1000"}));
  WiFi.scan = WIFI_SCAN_FAILED;
  assert(wifi().scanComplete() == SCAN_FAILED);
  WifiNetwork result;
  assert(!wifi().scanResult(0, result));
  WiFi.scan = 1;
  WiFi.name = std::string(32, 'x');
  assert(wifi().scanResult(0, result));
  assert(std::strlen(result.ssid) == 32 && result.encrypted && result.signalDbm == -63);
  assert(!wifi().scanResult(-1, result) && !wifi().scanResult(1, result));
  assert(result.ssid[0] == 0);

  calls.clear();
  wifi().connect("test", "secret");
  assert(WiFi.name == "test" && WiFi.password == "secret");
  assert(WiFi.hostname == "CrossPoint-Reader-AABBCCDDEEFF");
  assert(WiFi.currentMode == WIFI_STA); // MAC and hostname must not run with Wi-Fi disabled.
  assert((calls == std::vector<std::string>{"persistent:off", "mode:1", "disconnect:01", "delay:100",
                                          "hostname", "connect"}));
  calls.clear();
  wifi().connect("open", nullptr);
  assert(WiFi.password.empty() && WiFi.currentMode == WIFI_STA);
  assert((calls == std::vector<std::string>{"persistent:off", "mode:1", "disconnect:01", "delay:100",
                                          "hostname", "connect"}));
  calls.clear();
  wifi().connect(nullptr, nullptr);
  assert(calls.empty());
  wifi().clearScan();
  assert((calls == std::vector<std::string>{"scan.clear"})); // Child exit does not power down parent network.

  calls.clear();
  assert(wifi().startAccessPoint("transfer", nullptr, 1, 4));
  assert(WiFi.password.empty());
  assert((calls == std::vector<std::string>{"mode:2", "delay:100", "ap:1:0:4", "delay:100"}));
  char address[16];
  wifi().accessPointAddress(address);
  assert(std::strcmp(address, "192.168.4.1") == 0);
  calls.clear();
  shutdown();
  assert((calls == std::vector<std::string>{"clock.stop", "ap.stop", "disconnect:00", "delay:100", "mode:0", "delay:100"}));
  calls.clear();
  WiFi.currentMode = WIFI_STA;
  shutdown();
  assert((calls == std::vector<std::string>{"clock.stop", "disconnect:00", "delay:100", "mode:0", "delay:100"}));
  std::cout << "Runtime network provider tests passed\n";
}
