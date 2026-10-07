/* Transitional raw FSPI port. SPIClass remains the single physical controller
 * owner shared with the resident LoRa adapter. Its existing patched locks and
 * stalled-controller retention are preserved. Only the SPI bus ELF imports
 * these functions; no peripheral command, response, retry or filesystem here.
 */
#if defined(BOARD_T5S3_PRO)
#include <Arduino.h>
#include <BoardT5S3.h>
#include <SPI.h>
#include <SdSpiFault.h>
#include <RiscFirmwareSpiCompatV1.h>
#include <RiscSpiBusV1.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>

namespace {
TaskHandle_t owner = nullptr;
uint32_t began;
size_t transferred;
bool owns() { return owner && owner == xTaskGetCurrentTaskHandle(); }
bool live() { return owns() && millis() - began < RISC_SPI_SESSION_MAX_MS; }
}

extern "C" bool risc_fw_spi_begin_v1(uint8_t chipSelect, uint32_t hz, bool selected) {
  // This transitional profile admits only the SD slot. LoRa still borrows the
  // same SPIClass transaction lock through its resident adapter; it cannot be
  // independently claimed through this port until that client is extracted.
  if (owner || chipSelect != T5S3_SD_CS || hz < 100000 || hz > 25000000 || risc_sd_spi_faulted()) return false;
  // Board boot initializes FSPI once before external storage activation.
  // No SPI.begin/end, controller reset, or rail change at session admission.
  SPI.beginTransaction(SPISettings(hz, MSBFIRST, SPI_MODE0));
  owner = xTaskGetCurrentTaskHandle();
  began = millis();
  transferred = 0;
  digitalWrite(chipSelect, selected ? LOW : HIGH);
  return true;
}
extern "C" bool risc_fw_spi_select_v1(bool selected) {
  if (!live()) return false;
  digitalWrite(T5S3_SD_CS, selected ? LOW : HIGH);
  return true;
}
extern "C" bool risc_fw_spi_transfer_v1(const uint8_t* tx, uint8_t* rx, size_t bytes) {
  if (!live() || !bytes || bytes > RISC_SPI_TRANSFER_MAX ||
      bytes > RISC_SPI_SESSION_MAX_BYTES - transferred) return false;
  uint8_t fill[64];
  std::memset(fill, 0xff, sizeof(fill));
  for (size_t at = 0; at < bytes;) {
    if (!live()) return false;
    const size_t amount = bytes - at < sizeof(fill) ? bytes - at : sizeof(fill);
    SPI.transferBytes(tx ? tx + at : fill, rx ? rx + at : nullptr, amount);
    at += amount;
    transferred += amount;
    // SPI's guarded FIFO waits retain uncertain ownership on a physical stall.
    // Ordinary completed transfers cooperate without relinquishing the bus.
    if ((transferred & 4095u) == 0) delay(1);
  }
  return live();
}
extern "C" bool risc_fw_spi_end_v1() {
  if (!owns()) return false;
  // Session deadline does not prevent releasing completed synchronous work.
  // A physical stall cannot arrive here: the existing port parks its owner.
  digitalWrite(T5S3_SD_CS, HIGH);
  SPI.endTransaction();
  owner = nullptr;
  return true;
}
#endif
