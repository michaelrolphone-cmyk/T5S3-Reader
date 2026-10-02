#include "BoardX4Pro.h"

namespace BoardX4Pro {
namespace {
const BoardCapabilities kCaps{};
const BatteryProfile kProfile{};
}
const char* id() { return "xteink-x4-pro"; }
const char* displayName() { return "Xteink X4 Pro"; }
const char* firmwareMarker() { return "RISCRTE_BOARD_ID:xteink-x4-pro"; }
const BoardCapabilities& capabilities() { return kCaps; }
void begin() {}
void beginI2C() {}
void initBacklight() {}
void setBacklightLevel(uint8_t) {}
void restoreBacklightLevel(uint8_t) {}
void prepareSdBus() {}
void disableGpsLora() {}
void deinitForSleep() {}
const BatteryProfile& batteryProfile() { return kProfile; }
bool beginBatteryManagement() { return false; }
bool isBatteryManagementReady() { return false; }
bool readBatteryState(BatteryState* state) {
  if (state) *state = BatteryState{};
  return false;
}
bool shutdownBatteryPower() { return false; }
bool pca9535Present() { return false; }
bool readPca9535Pin(uint8_t, bool* high) {
  if (high) *high = false;
  return false;
}
bool writePca9535Pin(uint8_t, bool) { return false; }
bool setPca9535PinMode(uint8_t, uint8_t) { return false; }
bool readButton() { return false; }
bool readBQ27220Reg16(uint8_t, uint16_t* value) {
  if (value) *value = 0;
  return false;
}
bool readBQ25896Reg8(uint8_t, uint8_t* value) {
  if (value) *value = 0;
  return false;
}
bool readBatteryStateOfCharge(uint16_t* soc) {
  if (soc) *soc = 0;
  return false;
}
bool readBatteryCurrentMa(int16_t* current) {
  if (current) *current = 0;
  return false;
}
bool readBatteryAverageCurrentMa(int16_t* current) {
  if (current) *current = 0;
  return false;
}
bool isUsbConnected() { return false; }
}
