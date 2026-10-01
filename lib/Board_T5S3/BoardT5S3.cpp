#include <HalStorageLifecycle.h>
#include "BoardT5S3.h"
#include "BoardPowerPort.h"

#include <cassert>
#include <bq27220.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <SPI.h>
#include <Wire.h>

namespace BoardT5S3 {
namespace {
constexpr uint8_t PCA_REG_INPUT0 = 0x00;
constexpr uint8_t PCA_REG_OUTPUT0 = 0x02;
constexpr uint8_t PCA_REG_CONFIG0 = 0x06;
constexpr uint8_t kBacklightPwmChannel = 0;
constexpr uint8_t kBacklightPwmResolutionBits = 8;
constexpr uint32_t kBacklightPwmFrequencyHz = 5000;

constexpr BatteryProfile kBatteryProfile = {
    .inputLimitMa = 1000,
    .capacityMah = 1500,
    .chargeCurrentMa = 512,
    .prechargeCurrentMa = 64,
    .terminationCurrentMa = 64,
    .chargeVoltageMv = 4208,
    .chargeTerminationVoltageDeltaMv = 100,
    .systemMinVoltageMv = 3300,
    .currentThresholdMa = 20,
};

constexpr BoardCapabilities kCapabilities = {
    .hasBacklight = true,
    .hasDetailedBatteryTelemetry = true,
    .hasHardPowerOff = true,
    .hasTouchWake = true,
    .hasTouch = true,
    .hasRtc = true,
};

bool gaugeInitAttempted = false;
bool chargerConfigured = false;
bool bq27220Ready = false;
BQ27220 bq27220;
bool backlightInitialized = false;
SemaphoreHandle_t i2cMutex = nullptr;

void prepareTouchControllerForProvider() {
  // Board-only electrical bootstrap: GT911 samples INT while RESET rises.
  // Runtime register access and READY acknowledgement belong exclusively to
  // the installed input.touch.raw provider.
  pinMode(T5S3_TOUCH_INT, OUTPUT);
  digitalWrite(T5S3_TOUCH_INT, LOW);  // Select the board's documented 0x5D address.
  pinMode(T5S3_TOUCH_RST, OUTPUT);
  digitalWrite(T5S3_TOUCH_RST, LOW);
  delay(20);
  digitalWrite(T5S3_TOUCH_RST, HIGH);
  delay(60);
  pinMode(T5S3_TOUCH_INT, INPUT);
  delay(5);
}

SemaphoreHandle_t ensureI2CMutex() {
  if (i2cMutex == nullptr) {
    i2cMutex = xSemaphoreCreateRecursiveMutex();
    assert(i2cMutex != nullptr && "Failed to create I2C mutex");
  }
  return i2cMutex;
}

bool i2cWriteReg(uint8_t addr, uint8_t reg, const uint8_t* data, size_t len) {
  ScopedI2CLock lock;
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (data != nullptr && len > 0) {
    Wire.write(data, len);
  }
  return Wire.endTransmission() == 0;
}

bool i2cReadReg(uint8_t addr, uint8_t reg, uint8_t* data, size_t len) {
  ScopedI2CLock lock;
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  const uint8_t requested = static_cast<uint8_t>(len);
  if (Wire.requestFrom(addr, requested) != requested) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    data[i] = Wire.read();
  }
  return true;
}

bool updatePca9535Bit(uint8_t baseReg, uint8_t pin, bool high) {
  // PCA9535 bit writes are read-modify-write operations. Keep that pair
  // atomic, but release the shared bus immediately afterwards so unrelated
  // clients such as the GT911 touch provider are not starved by display work.
  ScopedI2CLock lock;
  const uint8_t port = pin / 8;
  const uint8_t bit = pin % 8;
  uint8_t value = 0;
  if (!i2cReadReg(T5S3_PCA9535_ADDR, baseReg + port, &value, 1)) {
    return false;
  }
  if (high) {
    value |= static_cast<uint8_t>(1U << bit);
  } else {
    value &= static_cast<uint8_t>(~(1U << bit));
  }
  return i2cWriteReg(T5S3_PCA9535_ADDR, baseReg + port, &value, 1);
}

bool readReg16LE(uint8_t addr, uint8_t reg, uint16_t* value) {
  uint8_t data[2] = {0, 0};
  if (!i2cReadReg(addr, reg, data, sizeof(data))) {
    return false;
  }
  *value = static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
  return true;
}

i2c_master_bus_handle_t i2cMasterBusHandle() { return reinterpret_cast<i2c_master_bus_handle_t>(&Wire); }


uint8_t backlightDutyForLevel(uint8_t level) {
  if (level == 0) {
    return 0;
  }
  if (level > 10) {
    level = 10;
  }

  const uint32_t levelSquared = static_cast<uint32_t>(level) * static_cast<uint32_t>(level);
  const uint32_t duty = (levelSquared * 255U + 50U) / 100U;
  return static_cast<uint8_t>(duty > 255U ? 255U : duty);
}

bool configureBq27220() {
  if (!bq27220.begin(i2cMasterBusHandle(), T5S3_BQ27220_ADDR, T5S3_I2C_FREQ)) {
    return false;
  }

  if (!bq27220.setDefaultCapacity(kBatteryProfile.capacityMah) ||
      !bq27220.setChargeParameters(kBatteryProfile.chargeCurrentMa, kBatteryProfile.chargeVoltageMv,
                                   kBatteryProfile.terminationCurrentMa,
                                   kBatteryProfile.chargeTerminationVoltageDeltaMv) ||
      !bq27220.init()) {
    bq27220.end();
    return false;
  }
  return true;
}

}  // namespace

const char* id() { return "t5s3-pro"; }

const char* displayName() { return "LilyGo T5S3 E-Paper PRO/Lite"; }

const char* firmwareMarker() { return "RISCRTE_BOARD_ID:t5s3-pro"; }

const BoardCapabilities& capabilities() { return kCapabilities; }

ScopedI2CLock::ScopedI2CLock() {
  locked_ = xSemaphoreTakeRecursive(ensureI2CMutex(), portMAX_DELAY) == pdTRUE;
}

ScopedI2CLock::ScopedI2CLock(uint32_t timeoutMs) {
  TickType_t ticks = pdMS_TO_TICKS(timeoutMs);
  if (timeoutMs && !ticks) ticks = 1;
  locked_ = xSemaphoreTakeRecursive(ensureI2CMutex(), ticks) == pdTRUE;
}

ScopedI2CLock::~ScopedI2CLock() {
  if (locked_) {
    xSemaphoreGiveRecursive(ensureI2CMutex());
    locked_ = false;
  }
}

void beginI2C() {
  ensureI2CMutex();
  Wire.begin(T5S3_SDA, T5S3_SCL);
  Wire.setClock(T5S3_I2C_FREQ);
  Wire.setTimeOut(50);
}

void initBacklight() {
  if (backlightInitialized) {
    return;
  }

  ledcSetup(kBacklightPwmChannel, kBacklightPwmFrequencyHz, kBacklightPwmResolutionBits);
  ledcAttachPin(T5S3_BL_EN, kBacklightPwmChannel);
  backlightInitialized = true;
  ledcWrite(kBacklightPwmChannel, 0);
}

void setBacklightLevel(uint8_t level) {
  if (!backlightInitialized) {
    initBacklight();
  }
  ledcWrite(kBacklightPwmChannel, backlightDutyForLevel(level));
}

void restoreBacklightLevel(uint8_t level) {
  // The guest may have attached the same GPIO to another LEDC channel.
  ledcSetup(kBacklightPwmChannel, kBacklightPwmFrequencyHz, kBacklightPwmResolutionBits);
  ledcAttachPin(T5S3_BL_EN, kBacklightPwmChannel);
  backlightInitialized = true;
  ledcWrite(kBacklightPwmChannel, backlightDutyForLevel(level));
}

void prepareSdBus() {
  pinMode(T5S3_LORA_CS, OUTPUT);
  digitalWrite(T5S3_LORA_CS, HIGH);
  pinMode(T5S3_SD_CS, OUTPUT);
  digitalWrite(T5S3_SD_CS, HIGH);
  SPI.begin(T5S3_SPI_SCLK, T5S3_SPI_MISO, T5S3_SPI_MOSI, T5S3_SD_CS);
}

void disableGpsLora() {
  pinMode(T5S3_LORA_CS, OUTPUT);
  digitalWrite(T5S3_LORA_CS, HIGH);
  pinMode(T5S3_LORA_RST, OUTPUT);
  digitalWrite(T5S3_LORA_RST, LOW);
  pinMode(T5S3_LORA_IRQ, INPUT);
  pinMode(T5S3_LORA_BUSY, INPUT);
  pinMode(T5S3_GPS_RXD, INPUT);
  pinMode(T5S3_GPS_TXD, INPUT);

  writePca9535Pin(PCA9535_IO00_LORA_GPS_EN, false);
  setPca9535PinMode(PCA9535_IO00_LORA_GPS_EN, OUTPUT);
}

void begin() {
  beginI2C();
  prepareTouchControllerForProvider();
  initBacklight();
  setBacklightLevel(0);

  pinMode(T5S3_BOOT_BTN, INPUT_PULLUP);
  if (T5S3_PCA9535_INT > 0) {
    pinMode(T5S3_PCA9535_INT, INPUT_PULLUP);
  }

  prepareSdBus();
  disableGpsLora();
  // The expander button is a fixed input. Configure it once at board startup;
  // rewriting PCA9535 direction on every UI frame adds two avoidable I2C
  // transactions to the input hot path.
  (void)setPca9535PinMode(PCA9535_IO12_BUTTON, INPUT);
}

void deinitForSleep() {
  // Pin the installed owner while SD is still available.
  (void)BoardPowerPort::prepareShutdown();
  halStorageMediaUnavailable(); // Existing SD bus shutdown invalidates retained metadata.
  setBacklightLevel(0);
  pinMode(T5S3_BL_EN, OUTPUT);
  digitalWrite(T5S3_BL_EN, LOW);
  disableGpsLora();
  pinMode(T5S3_SD_CS, INPUT);
  pinMode(T5S3_GPS_RXD, INPUT);
  pinMode(T5S3_GPS_TXD, INPUT);
}

const BatteryProfile& batteryProfile() { return kBatteryProfile; }

bool beginBatteryManagement() {
  // Gauge is a separate chip and remains accessible during early boot.
  // A pre-mount call must not attempt to map an ELF or latch charger failure.
  if (!gaugeInitAttempted) {
    gaugeInitAttempted = true;
    bq27220Ready = configureBq27220();
  }
  if (!chargerConfigured && BoardPowerPort::readyForActivation())
    chargerConfigured = BoardPowerPort::configure();
  return chargerConfigured || bq27220Ready;
}

bool isBatteryManagementReady() { return chargerConfigured || bq27220Ready; }

bool shutdownBatteryPower() {
  // Installed owner always checks live input, OTG leases and uncertain writes.
  // No BQ25896 register-level firmware fallback exists.
  return BoardPowerPort::shutdown();
}

bool readBatteryState(BatteryState* state) {
  if (!state) return false;
  *state = {};
  state->gaugeReady = bq27220Ready;
  state->gaugeChargeVoltageMv = kBatteryProfile.chargeVoltageMv;
  state->gaugeTaperCurrentMa = kBatteryProfile.terminationCurrentMa;
  const bool chargerAvailable = BoardPowerPort::read(state);
  state->chargerReady = chargerAvailable;

  if (bq27220Ready) {
    BQ27220Snapshot gauge = {};
    state->gaugeReadOk = bq27220.readSnapshot(&gauge);
    if (state->gaugeReadOk) {
      const bool inferredVbus = state->vbusConnected || gauge.charging;
      state->gaugeState = static_cast<BatteryGaugeState>(
          BQ27220::classifyState(&gauge, inferredVbus, kBatteryProfile.currentThresholdMa));
      state->gaugeBatteryFullFlag = gauge.battery_status.reg.FC;
      state->gaugeGaugingFullFlag = gauge.gauging_status.reg.FC;
      state->gaugeTaperFlag = gauge.battery_status.reg.TCA;
      state->gaugeChargeInhibit = gauge.battery_status.reg.CHGINH;
      state->gaugeVoltageMv = gauge.voltage_mv;
      state->currentMa = gauge.current_ma;
      state->averageCurrentMa = gauge.average_current_ma;
      state->socPercent = gauge.soc;
      state->sohPercent = gauge.soh_percent;
      state->fullCapacityMah = gauge.fcc_mah;
      state->remainingCapacityMah = gauge.remaining_capacity_mah;
      state->temperatureDk = gauge.temperature_dk;
      state->batteryStatusRaw = gauge.battery_status.full;
      state->gaugingStatusRaw = gauge.gauging_status.full;
      state->charging = state->charging || gauge.charging;
      state->chargeDone = state->chargeDone || gauge.full || gauge.battery_status.reg.TCA;
      if (!state->chargerReadOk) state->vbusConnected = inferredVbus;
    }
  }
  return state->chargerReadOk || state->gaugeReadOk;
}

bool pca9535Present() {
  ScopedI2CLock lock;
  Wire.beginTransmission(T5S3_PCA9535_ADDR);
  return Wire.endTransmission() == 0;
}

bool setPca9535PinMode(uint8_t pin, uint8_t mode) {
  const bool inputMode = mode != OUTPUT;
  return updatePca9535Bit(PCA_REG_CONFIG0, pin, inputMode);
}

bool writePca9535Pin(uint8_t pin, bool high) {
  return updatePca9535Bit(PCA_REG_OUTPUT0, pin, high);
}

bool readPca9535Pin(uint8_t pin, bool* high) {
  if (!high) {
    return false;
  }
  const uint8_t port = pin / 8;
  const uint8_t bit = pin % 8;
  uint8_t value = 0;
  if (!i2cReadReg(T5S3_PCA9535_ADDR, PCA_REG_INPUT0 + port, &value, 1)) {
    return false;
  }
  *high = (value & (1U << bit)) != 0;
  return true;
}

bool readButton() {
  bool high = true;
  if (!readPca9535Pin(PCA9535_IO12_BUTTON, &high)) return false;
  return !high;
}

bool readBQ27220Reg16(uint8_t reg, uint16_t* value) {
  if (!value) {
    return false;
  }
  return readReg16LE(T5S3_BQ27220_ADDR, reg, value);
}

bool readBQ25896Reg8(uint8_t, uint8_t*) {
  // Compatibility function intentionally fails closed. Raw BQ reads are no
  // longer a firmware API, even for the former USB power-detection shortcut.
  return false;
}

bool readBatteryStateOfCharge(uint16_t* soc) {
  return readBQ27220Reg16(CommandStateOfCharge, soc);
}

bool readBatteryCurrentMa(int16_t* current) {
  if (!current) {
    return false;
  }
  uint16_t raw = 0;
  if (!readBQ27220Reg16(CommandCurrent, &raw)) {
    return false;
  }
  *current = static_cast<int16_t>(raw);
  return true;
}

bool readBatteryAverageCurrentMa(int16_t* current) {
  if (!current) {
    return false;
  }
  uint16_t raw = 0;
  if (!readBQ27220Reg16(CommandAverageCurrent, &raw)) {
    return false;
  }
  *current = static_cast<int16_t>(raw);
  return true;
}

bool isUsbConnected() {
  // GPIO polls this every loop. Avoid reloading the installed ELF and making
  // eleven I2C reads on every button/touch scan; keep physical safety checks
  // inside the ELF fresh (the shutdown path NEVER consumes this UI cache).
  static bool sampled = false;
  static bool connected = false;
  static unsigned long lastSampleMs = 0;
  static bool attemptedStartupCharge = false;
  static unsigned long lastChargeAttemptMs = 0;
  const unsigned long now = millis();
  if (!chargerConfigured && BoardPowerPort::readyForActivation() &&
      (!attemptedStartupCharge || now - lastChargeAttemptMs >= 30000UL)) {
    attemptedStartupCharge = true;
    lastChargeAttemptMs = now;
    chargerConfigured = BoardPowerPort::configure();
  }
  if (sampled && now - lastSampleMs < 1000UL) return connected;
  bool external = false;
  if (BoardPowerPort::externalPower(&external)) {
    connected = external;
  }
  // A failed provider read or stale fuel-gauge current is not a cable edge.
  // Retain the last observed state; missing ELF never enables a raw fallback.
  lastSampleMs = now;
  sampled = true;
  return connected;
}


}  // namespace BoardT5S3
