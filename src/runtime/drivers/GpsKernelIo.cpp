#include "GpsKernelIo.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include "runtime/resources/RadioPower.h"
#if defined(BOARD_T5S3_PRO)
#include <BoardT5S3.h>
#endif
namespace {
std::atomic<bool> claimed{false};
TaskHandle_t owner = nullptr;
bool uartOpen = false;
bool ownsEndpoint() { return claimed.load() && owner == xTaskGetCurrentTaskHandle(); }
uint32_t ticks(void*) { return millis(); }
void sleepMs(void*, uint32_t ms) { if (ownsEndpoint()) delay(ms); }
bool powerAcquire(void*) { return ownsEndpoint() && RadioPower::acquire(RadioPower::Owner::Gps); }
void powerRelease(void*) { if (ownsEndpoint()) RadioPower::release(RadioPower::Owner::Gps); }
void serialClose(void*) {
#if defined(BOARD_T5S3_PRO)
  if (!ownsEndpoint() || !uartOpen) return;
  T5S3_SerialGPS.end();
  pinMode(T5S3_GPS_RXD, INPUT);
  pinMode(T5S3_GPS_TXD, INPUT);
#endif
  uartOpen = false;
}
bool serialOpen(void*, uint32_t baud) {
#if defined(BOARD_T5S3_PRO)
  if (!ownsEndpoint() || (baud != 9600 && baud != 38400)) return false;
  serialClose(nullptr);
  T5S3_SerialGPS.begin(baud, SERIAL_8N1, T5S3_GPS_RXD, T5S3_GPS_TXD);
  uartOpen = static_cast<bool>(T5S3_SerialGPS);
  return uartOpen;
#else
  (void)baud;
  return false;
#endif
}
size_t serialRead(void*, uint8_t* buffer, size_t capacity) {
  if (!ownsEndpoint() || !uartOpen || !buffer) return 0;
  size_t count = 0;
#if defined(BOARD_T5S3_PRO)
  while (count < capacity && T5S3_SerialGPS.available()) {
    const int byte = T5S3_SerialGPS.read();
    if (byte < 0) break;
    buffer[count++] = static_cast<uint8_t>(byte);
  }
#endif
  return count;
}
const t5_kernel_io_v1 api = {T5_KERNEL_IO_API_VERSION, sizeof(t5_kernel_io_v1), nullptr, ticks, sleepMs,
                            powerAcquire, powerRelease, serialOpen, serialClose, serialRead};
}
bool gpsKernelAvailable() {
#if defined(BOARD_T5S3_PRO)
  return true;
#else
  return false;
#endif
}
const t5_kernel_io_v1* gpsKernelClaim() {
  if (!gpsKernelAvailable() || claimed.exchange(true)) return nullptr;
  owner = xTaskGetCurrentTaskHandle();
  return &api;
}
void gpsKernelRelease() {
  if (!ownsEndpoint()) return;
  serialClose(nullptr);
  powerRelease(nullptr);
  owner = nullptr;
  claimed.store(false);
}
