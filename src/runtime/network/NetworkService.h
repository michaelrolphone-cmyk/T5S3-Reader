#pragma once

#include <cstdint>

// Application-facing contracts. No Arduino, ESP-IDF, or board types cross this
// boundary. These APIs describe the current compiled-in provider; runtime
// discovery, handles, and manifest gating are subsequent migration steps.
namespace RuntimeNetwork {
constexpr uint32_t INTERFACE_API_VERSION = 1;
constexpr uint32_t WIFI_CONTROL_API_VERSION = 1;
constexpr int16_t SCAN_RUNNING = -1;
constexpr int16_t SCAN_FAILED = -2;

enum class ConnectionState { Disconnected, Connecting, Connected, Failed, NetworkNotFound };

struct InterfaceState {
  ConnectionState connection = ConnectionState::Disconnected;
  bool hasAddress = false;
  char address[16] = {};
};

struct WifiNetwork {
  char ssid[33] = {};
  int32_t signalDbm = 0;
  bool encrypted = false;
};

// network.default: generic link state and lifecycle, independent of transport.
struct InterfaceApi {
  uint32_t apiVersion;
  uint32_t structSize;
  InterfaceState (*state)();
  void (*shutdown)();
};

// network.wifi.control: optional Wi-Fi-specific administration, separate from
// the generic network interface. Only Wi-Fi UI needs this contract.
struct WifiControlApi {
  uint32_t apiVersion;
  uint32_t structSize;
  void (*stationMode)();
  void (*macAddress)(uint8_t mac[6]);
  int16_t (*startScan)(bool passive, uint32_t maxMsPerChannel);
  int16_t (*scanComplete)();
  bool (*scanResult)(int16_t index, WifiNetwork& result);
  void (*clearScan)();
  void (*connect)(const char* ssid, const char* password);
  void (*disconnect)();
  int32_t (*signalDbm)();
  void (*stationSsid)(char ssid[33]);
  bool (*startAccessPoint)(const char* ssid, const char* password, uint8_t channel, uint8_t maxConnections);
  void (*accessPointAddress)(char address[16]);
};

// Binding lives in the runtime, never in an activity. API lookup fails on
// incompatible versions. The initial provider has firmware lifetime.
const InterfaceApi* interfaceApi(uint32_t version);
const WifiControlApi* wifiControlApi(uint32_t version);

InterfaceState state();
bool connected();
bool ready();
void shutdown();
const WifiControlApi& wifi();
}  // namespace RuntimeNetwork
