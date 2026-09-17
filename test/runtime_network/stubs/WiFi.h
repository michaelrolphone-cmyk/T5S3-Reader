#pragma once
#include <Arduino.h>
#include <array>
#include <cassert>
#include <cstring>
constexpr int WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP = 2;
// Deliberately different from runtime values: the provider must translate.
constexpr int WIFI_SCAN_RUNNING = -17, WIFI_SCAN_FAILED = -18;
constexpr int WIFI_AUTH_OPEN = 0;
enum { WL_CONNECTED = 3, WL_CONNECT_FAILED = 4, WL_NO_SSID_AVAIL = 1, WL_IDLE_STATUS = 0, WL_DISCONNECTED = 6 };
class IPAddress {
  std::array<uint8_t, 4> bytes;
 public:
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : bytes{a,b,c,d} {}
  uint8_t operator[](int i) const { return bytes[i]; }
  bool operator!=(const IPAddress& other) const { return bytes != other.bytes; }
};
struct FakeWiFi {
  int linkStatus = WL_DISCONNECTED, currentMode = WIFI_OFF, scan = WIFI_SCAN_RUNNING;
  IPAddress ip{0,0,0,0};
  std::string name = "network", password, hostname;
  int status() { return linkStatus; }
  IPAddress localIP() { return ip; }
  IPAddress softAPIP() { return IPAddress(192,168,4,1); }
  int getMode() { return currentMode; }
  void mode(int mode) { currentMode = mode; calls.push_back("mode:" + std::to_string(mode)); }
  void persistent(bool flag) { calls.push_back(flag ? "persistent:on" : "persistent:off"); }
  void setSleep(bool flag) { calls.push_back(flag ? "sleep:on" : "sleep:off"); }
  void disconnect(bool off = false, bool erase = false) {
    calls.push_back(std::string("disconnect:") + (off ? "1" : "0") + (erase ? "1" : "0"));
    if (off) currentMode = WIFI_OFF;
  }
  void softAPdisconnect(bool) { calls.push_back("ap.stop"); }
  void macAddress(uint8_t* mac) { assert(currentMode != WIFI_OFF); std::memset(mac, 0xab, 6); }
  String macAddress() { assert(currentMode != WIFI_OFF); return String("AA:BB:CC:DD:EE:FF"); }
  void scanDelete() { calls.push_back("scan.clear"); }
  int16_t scanNetworks(bool async, bool hidden, bool passive, uint32_t ms) {
    calls.push_back(std::string("scan:") + (async ? "1" : "0") + (hidden ? "1" : "0") +
                    (passive ? "1:" : "0:") + std::to_string(ms));
    return scan;
  }
  int16_t scanComplete() { return scan; }
  String SSID(int = 0) { return String(name.c_str()); }
  int32_t RSSI(int = 0) { return -63; }
  int encryptionType(int) { return 1; }
  void setHostname(const char* host) {
    assert(currentMode != WIFI_OFF);
    hostname = host;
    calls.push_back("hostname");
  }
  void begin(const char* ssid, const char* pass = nullptr) {
    name = ssid; password = pass ? pass : ""; calls.push_back("connect");
  }
  bool softAP(const char* ssid, const char* pass, uint8_t channel, bool hidden, uint8_t max) {
    name = ssid; password = pass ? pass : "";
    calls.push_back("ap:" + std::to_string(channel) + ":" + (hidden ? "1:" : "0:") + std::to_string(max));
    return true;
  }
};
extern FakeWiFi WiFi;
