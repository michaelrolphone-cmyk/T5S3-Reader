#include "BoardEPD47.h"

#include <cassert>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <SPI.h>
#include <Wire.h>
#include <epd_driver.h>

namespace BoardEPD47 {
namespace {

constexpr BoardCapabilities kCapabilities = {
    .hasBacklight = false,
    .hasDetailedBatteryTelemetry = false,
    .hasHardPowerOff = false,
    .hasTouchWake = false,
    .hasTouch = true,
    .hasRtc = true,
};

constexpr BatteryProfile kBatteryProfile = {};
SemaphoreHandle_t i2cMutex = nullptr;

SemaphoreHandle_t ensureI2CMutex() {
  if (i2cMutex == nullptr) {
    i2cMutex = xSemaphoreCreateRecursiveMutex();
    assert(i2cMutex != nullptr && "Failed to create I2C mutex");
  }
  return i2cMutex;
}

uint16_t batteryPercentForMv(const uint16_t millivolts) {
  struct CurvePoint {
    uint16_t millivolts;
    uint8_t percent;
  };
  static constexpr CurvePoint curve[] = {
      {3300, 0}, {3500, 5}, {3600, 10}, {3700, 25}, {3800, 45},
      {3900, 65}, {4000, 80}, {4100, 92}, {4200, 100},
  };

  if (millivolts <= curve[0].millivolts) {
    return curve[0].percent;
  }
  for (size_t i = 1; i < sizeof(curve) / sizeof(curve[0]); ++i) {
    if (millivolts <= curve[i].millivolts) {
      const uint32_t voltageOffset = millivolts - curve[i - 1].millivolts;
      const uint32_t voltageSpan = curve[i].millivolts - curve[i - 1].millivolts;
      const uint32_t percentSpan = curve[i].percent - curve[i - 1].percent;
      return curve[i - 1].percent + (voltageOffset * percentSpan + voltageSpan / 2) / voltageSpan;
    }
  }
  return 100;
}

uint16_t readBatteryMillivolts() {
  // GPIO14's divider is supplied by the EPD power domain on this board.
  epd_poweron();
  delay(10);
  analogSetPinAttenuation(EPD47_BATTERY_ADC, ADC_11db);
  uint32_t pinMillivolts = 0;
  constexpr uint8_t sampleCount = 8;
  for (uint8_t i = 0; i < sampleCount; ++i) {
    pinMillivolts += analogReadMilliVolts(EPD47_BATTERY_ADC);
  }
  pinMillivolts = (pinMillivolts + sampleCount / 2) / sampleCount;
  epd_poweroff_all();
  return static_cast<uint16_t>(min<uint32_t>(pinMillivolts * 2U, 5000U));
}
}  // namespace

const char* id() { return "lilygo-epd47-s3"; }

const char* displayName() { return "LilyGo EPD47 ESP32-S3"; }

const char* firmwareMarker() { return "RISCRTE_BOARD_ID:lilygo-epd47-s3"; }

const BoardCapabilities& capabilities() { return kCapabilities; }

ScopedI2CLock::ScopedI2CLock() {
  xSemaphoreTakeRecursive(ensureI2CMutex(), portMAX_DELAY);
  locked_ = true;
}

ScopedI2CLock::~ScopedI2CLock() {
  if (locked_) {
    xSemaphoreGiveRecursive(ensureI2CMutex());
    locked_ = false;
  }
}

void beginI2C() {
  ensureI2CMutex();
  Wire.begin(EPD47_I2C_SDA, EPD47_I2C_SCL);
  Wire.setClock(EPD47_I2C_FREQ);
  Wire.setTimeOut(50);
}

void initBacklight() {}

void setBacklightLevel(uint8_t level) { (void)level; }

void prepareSdBus() {
  pinMode(EPD47_SD_CS, OUTPUT);
  digitalWrite(EPD47_SD_CS, HIGH);
  SPI.begin(EPD47_SD_SCLK, EPD47_SD_MISO, EPD47_SD_MOSI, EPD47_SD_CS);
}

void disableGpsLora() {}

void begin() {
  beginI2C();
  pinMode(EPD47_BUTTON, INPUT_PULLUP);
  // EPD47 ties GT911 RESET high. Pulse INT high to wake the controller; all
  // register I/O and READY acknowledgement are owned by input.touch.raw.
  pinMode(EPD47_TOUCH_INT, OUTPUT);
  digitalWrite(EPD47_TOUCH_INT, HIGH);
  delay(5);
  pinMode(EPD47_TOUCH_INT, INPUT_PULLUP);
  delay(50);
  pinMode(EPD47_BATTERY_ADC, INPUT);
  prepareSdBus();
}

void deinitForSleep() {
  pinMode(EPD47_SD_CS, INPUT);
  pinMode(EPD47_SD_MISO, INPUT);
  pinMode(EPD47_SD_MOSI, INPUT);
  pinMode(EPD47_SD_SCLK, INPUT);
  pinMode(EPD47_I2C_SDA, OPEN_DRAIN);
  pinMode(EPD47_I2C_SCL, OPEN_DRAIN);
  pinMode(EPD47_TOUCH_INT, INPUT);
}

const BatteryProfile& batteryProfile() { return kBatteryProfile; }

bool beginBatteryManagement() { return true; }

bool isBatteryManagementReady() { return true; }

bool readBatteryState(BatteryState* state) {
  if (state == nullptr) {
    return false;
  }
  *state = {};
  state->gaugeReady = true;
  state->gaugeReadOk = true;
  state->gaugeVoltageMv = readBatteryMillivolts();
  state->batteryVoltageMv = state->gaugeVoltageMv;
  state->socPercent = batteryPercentForMv(state->gaugeVoltageMv);
  state->gaugeState = state->socPercent >= 99 ? BatteryGaugeState::Full : BatteryGaugeState::Discharge;
  return true;
}

bool shutdownBatteryPower() { return false; }

bool pca9535Present() { return false; }

bool readPca9535Pin(uint8_t pin, bool* high) {
  (void)pin;
  if (high) {
    *high = false;
  }
  return false;
}

bool writePca9535Pin(uint8_t pin, bool high) {
  (void)pin;
  (void)high;
  return false;
}

bool setPca9535PinMode(uint8_t pin, uint8_t mode) {
  (void)pin;
  (void)mode;
  return false;
}

bool readButton() { return false; }

bool readBQ27220Reg16(uint8_t reg, uint16_t* value) {
  (void)reg;
  (void)value;
  return false;
}

bool readBQ25896Reg8(uint8_t reg, uint8_t* value) {
  (void)reg;
  (void)value;
  return false;
}

bool readBatteryStateOfCharge(uint16_t* soc) {
  if (soc == nullptr) {
    return false;
  }
  const uint16_t millivolts = readBatteryMillivolts();
  *soc = batteryPercentForMv(millivolts);
  return true;
}

bool readBatteryCurrentMa(int16_t* current) {
  (void)current;
  return false;
}

bool readBatteryAverageCurrentMa(int16_t* current) {
  (void)current;
  return false;
}

bool isUsbConnected() { return static_cast<bool>(Serial); }


}  // namespace BoardEPD47
