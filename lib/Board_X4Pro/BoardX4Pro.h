#pragma once
#include <Arduino.h>
#include <BoardCapabilities.h>

namespace BoardX4Pro {
const char* id();
const char* displayName();
const char* firmwareMarker();
const BoardCapabilities& capabilities();

class ScopedI2CLock {
 public:
  ScopedI2CLock() = default;
  explicit ScopedI2CLock(uint32_t) {}
  bool acquired() const { return false; }
  ~ScopedI2CLock() = default;
};

void begin();
void beginI2C();
void initBacklight();
void setBacklightLevel(uint8_t level);
void restoreBacklightLevel(uint8_t level);
void prepareSdBus();
void disableGpsLora();
void deinitForSleep();

struct BatteryProfile {
  uint16_t inputLimitMa = 0;
  uint16_t capacityMah = 1100;
  uint16_t chargeCurrentMa = 0;
  uint16_t prechargeCurrentMa = 0;
  uint16_t terminationCurrentMa = 0;
  uint16_t chargeVoltageMv = 0;
  uint16_t chargeTerminationVoltageDeltaMv = 0;
  uint16_t systemMinVoltageMv = 0;
  int16_t currentThresholdMa = 0;
};
enum class BatteryChargeStatus : uint8_t { NotCharging = 0, Unknown = 0xFF };
enum class BatteryGaugeState : uint8_t { Unknown = 0xFF };
struct BatteryState {
  bool chargerReady = false;
  bool gaugeReady = false;
  bool charging = false;
};
const BatteryProfile& batteryProfile();
bool beginBatteryManagement();
bool isBatteryManagementReady();
bool readBatteryState(BatteryState* state);
bool shutdownBatteryPower();
bool pca9535Present();
bool readPca9535Pin(uint8_t pin, bool* high);
bool writePca9535Pin(uint8_t pin, bool high);
bool setPca9535PinMode(uint8_t pin, uint8_t mode);
bool readButton();
bool readBQ27220Reg16(uint8_t reg, uint16_t* value);
bool readBQ25896Reg8(uint8_t reg, uint8_t* value);
bool readBatteryStateOfCharge(uint16_t* soc);
bool readBatteryCurrentMa(int16_t* current);
bool readBatteryAverageCurrentMa(int16_t* current);
bool isUsbConnected();
}

namespace BoardX4ProPins {
static constexpr uint8_t PowerButton = 3;
static constexpr uint8_t TouchInterrupt = 10;
static constexpr uint8_t SdCs = 0xFF;
}
