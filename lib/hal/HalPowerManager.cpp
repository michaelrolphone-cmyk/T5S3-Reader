#include "HalPowerManager.h"

#include <WiFi.h>

#include <cassert>

#include "HalGPIO.h"

HalPowerManager powerManager;  // Singleton instance

void HalPowerManager::begin() {
  Board::beginI2C();
  Board::beginBatteryManagement();
  normalFreq = getCpuFrequencyMhz();
  modeMutex = xSemaphoreCreateMutex();
  assert(modeMutex != nullptr);
}

void HalPowerManager::setPowerSaving(bool enabled) {
  if (normalFreq <= 0) {
    return;
  }

  auto wifiMode = WiFi.getMode();
  if (wifiMode != WIFI_MODE_NULL) {
    enabled = false;
  }

  const LockMode mode = currentLockMode;

  if (mode == None && enabled && !isLowPower) {
    LOG_DBG("PWR", "Going to low-power mode");
    if (!setCpuFrequencyMhz(LOW_POWER_FREQ)) {
      LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", LOW_POWER_FREQ);
      return;
    }
    isLowPower = true;
    LOG_INF("PWR", "Idle CPU clock: %d MHz", LOW_POWER_FREQ);
  } else if ((!enabled || mode != None) && isLowPower) {
    LOG_DBG("PWR", "Restoring normal CPU frequency");
    if (!setCpuFrequencyMhz(normalFreq)) {
      LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", normalFreq);
      return;
    }
    isLowPower = false;
  }
}

void HalPowerManager::startDeepSleep(HalGPIO& gpio, bool wakeOnTouch) const { gpio.startDeepSleep(wakeOnTouch); }

uint16_t HalPowerManager::getBatteryPercentage() const {
  const unsigned long now = millis();
  if (_batteryLastPollMs != 0 && (now - _batteryLastPollMs) < BATTERY_POLL_MS) {
    return _batteryCachedPercent;
  }

  uint16_t soc = 0;
  if (!Board::readBatteryStateOfCharge(&soc)) {
    _batteryLastPollMs = now;
    return _batteryCachedPercent;
  }

  _batteryCachedPercent = soc > 100 ? 100 : soc;
  _batteryLastPollMs = now;
  return _batteryCachedPercent;
}

bool HalPowerManager::readBatteryPercentage(uint16_t* percentage) const {
  if (!percentage) return false;
#if defined(BOARD_XTEINK_X4_PRO)
  // Never turn an absent, failed or expired optional gauge into a fake 0%.
  return Board::readBatteryStateOfCharge(percentage);
#else
  // Preserve the established T5/EPD47 cached presentation behavior.
  *percentage = getBatteryPercentage();
  return true;
#endif
}

bool HalPowerManager::isBatteryCharging() const {
#if defined(BOARD_XTEINK_X4_PRO)
  Board::BatteryState state{};
  return Board::readBatteryState(&state) && state.charging;
#else
  // Existing boards use the external-power indicator for this icon.
  return Board::isUsbConnected();
#endif
}

HalPowerManager::Lock::Lock() {
  xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY);
  if (powerManager.currentLockMode != None) {
    LOG_ERR("PWR", "Lock already held, ignore");
    valid = false;
  } else {
    powerManager.currentLockMode = NormalSpeed;
    valid = true;
  }
  xSemaphoreGive(powerManager.modeMutex);
  if (valid) {
    powerManager.setPowerSaving(false);
  }
}

HalPowerManager::Lock::~Lock() {
  xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY);
  if (valid) {
    powerManager.currentLockMode = None;
  }
  xSemaphoreGive(powerManager.modeMutex);
}
