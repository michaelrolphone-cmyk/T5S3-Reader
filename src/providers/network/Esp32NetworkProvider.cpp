#include "Esp32NetworkProvider.h"

#include <Arduino.h>
#include <WiFi.h>
#include <cstdio>
#include <cstring>

#include "ClockSync.h"

namespace Esp32NetworkProvider {
namespace {
using namespace RuntimeNetwork;

void formatAddress(const IPAddress& ip, char address[16]) {
  snprintf(address, 16, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

InterfaceState state() {
  InterfaceState result;
  switch (WiFi.status()) {
    case WL_CONNECTED: result.connection = ConnectionState::Connected; break;
    case WL_CONNECT_FAILED: result.connection = ConnectionState::Failed; break;
    case WL_NO_SSID_AVAIL: result.connection = ConnectionState::NetworkNotFound; break;
    case WL_IDLE_STATUS: result.connection = ConnectionState::Connecting; break;
    default: result.connection = ConnectionState::Disconnected; break;
  }
  const auto ip = WiFi.localIP();
  result.hasAddress = result.connection == ConnectionState::Connected && ip != IPAddress(0, 0, 0, 0);
  formatAddress(ip, result.address);
  return result;
}

void shutdown() {
  ClockSync::stop();
  if (WiFi.getMode() & WIFI_AP) WiFi.softAPdisconnect(true);
  WiFi.disconnect(false);
  delay(100);  // Let the disconnect frame leave before powering down.
  WiFi.mode(WIFI_OFF);
  delay(100);
}

void stationMode() { WiFi.mode(WIFI_STA); }
void macAddress(uint8_t mac[6]) { WiFi.macAddress(mac); }
void clearScan() { WiFi.scanDelete(); }

int16_t scanStatus(int16_t result) {
  if (result == WIFI_SCAN_RUNNING) return SCAN_RUNNING;
  if (result < 0) return SCAN_FAILED;
  return result;
}

int16_t startScan(bool passive, uint32_t maxMsPerChannel) {
  WiFi.scanDelete();
  WiFi.persistent(false);
  // ESP32-S3 needs a full STA restart to avoid stale/empty scan results.
  WiFi.mode(WIFI_OFF);
  delay(150);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect(false, false);
  delay(150);
  return scanStatus(WiFi.scanNetworks(true, true, passive, maxMsPerChannel));
}

int16_t scanComplete() { return scanStatus(WiFi.scanComplete()); }
bool scanResult(int16_t index, WifiNetwork& result) {
  result = {};
  const int16_t count = WiFi.scanComplete();
  if (index < 0 || index >= count) return false;
  snprintf(result.ssid, sizeof(result.ssid), "%s", WiFi.SSID(index).c_str());
  result.signalDbm = WiFi.RSSI(index);
  result.encrypted = WiFi.encryptionType(index) != WIFI_AUTH_OPEN;
  return true;
}

void connect(const char* ssid, const char* password) {
  if (!ssid || !ssid[0]) return;
  WiFi.persistent(false);  // The credential store owns persistence.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);  // Clear stale SDK auto-connect credentials.
  delay(100);
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  String hostname = "CrossPoint-Reader-" + mac;
  WiFi.setHostname(hostname.c_str());
  if (password && password[0]) WiFi.begin(ssid, password);
  else WiFi.begin(ssid);
}

void disconnect() { WiFi.disconnect(); }
int32_t signalDbm() { return WiFi.RSSI(); }
void stationSsid(char ssid[33]) { snprintf(ssid, 33, "%s", WiFi.SSID().c_str()); }

bool startAccessPoint(const char* ssid, const char* password, uint8_t channel, uint8_t maxConnections) {
  if (!ssid || !ssid[0]) return false;
  WiFi.mode(WIFI_AP);
  delay(100);
  const bool started = WiFi.softAP(ssid, password && strlen(password) >= 8 ? password : nullptr,
                                   channel, false, maxConnections);
  if (started) delay(100);
  return started;
}
void accessPointAddress(char address[16]) { formatAddress(WiFi.softAPIP(), address); }

const InterfaceApi kInterface = {INTERFACE_API_VERSION, sizeof(InterfaceApi), state, shutdown};
const WifiControlApi kWifi = {WIFI_CONTROL_API_VERSION, sizeof(WifiControlApi), stationMode, macAddress,
                            startScan, scanComplete, scanResult, clearScan, connect, disconnect,
                            signalDbm, stationSsid, startAccessPoint, accessPointAddress};
}  // namespace
const RuntimeNetwork::InterfaceApi& interfaceApi() { return kInterface; }
const RuntimeNetwork::WifiControlApi& wifiControlApi() { return kWifi; }
}  // namespace Esp32NetworkProvider
