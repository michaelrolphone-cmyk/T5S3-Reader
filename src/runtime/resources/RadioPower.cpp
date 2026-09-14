#include "RadioPower.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#if defined(BOARD_T5S3_PRO)
#include <BoardT5S3.h>
#endif
namespace RadioPower {
namespace {
StaticSemaphore_t mutexStorage;
SemaphoreHandle_t mutex = xSemaphoreCreateMutexStatic(&mutexStorage);
uint8_t owners = 0;
bool setRail(bool enabled) {
#if defined(BOARD_T5S3_PRO)
  return BoardT5S3::writePca9535Pin(PCA9535_IO00_LORA_GPS_EN, enabled) &&
         BoardT5S3::setPca9535PinMode(PCA9535_IO00_LORA_GPS_EN, OUTPUT);
#else
  (void)enabled;
  return false;
#endif
}
}
bool acquire(Owner owner) {
  if (!mutex || xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) return false;
  const bool ok = owners != 0 || setRail(true);
  if (ok) owners |= static_cast<uint8_t>(owner);
  xSemaphoreGive(mutex);
  return ok;
}
void release(Owner owner) {
  if (!mutex || xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) return;
  const uint8_t bit = static_cast<uint8_t>(owner);
  if (owners & bit) {
    owners &= ~bit;
    if (!owners) (void)setRail(false);
  }
  xSemaphoreGive(mutex);
}
}
