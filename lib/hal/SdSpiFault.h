#pragma once
#include <stdbool.h>
#include <stdint.h>

// Internal module-store port, never a provider API. Normal task context only.
// A latched fault has no reset/unlock/recovery API: only a manual reboot clears it.
#if defined(RISCRTE_SD_SPI_FAULT_PORT) || defined(RISCRTE_SD_SPI_FAULT_TEST)
#ifdef __cplusplus
extern "C" {
#endif
bool risc_sd_spi_faulted(void);
void risc_sd_spi_guard(void);
void risc_sd_spi_begin_operation(void);
void risc_sd_spi_end_operation(void);
void risc_sd_spi_busy(uint32_t started_ticks);
void risc_sd_spi_fail(const char* reason);
void risc_sd_spi_wait_lock(void* mutex);
#ifdef __cplusplus
}
#endif
#else
static inline bool risc_sd_spi_faulted(void) { return false; }
static inline void risc_sd_spi_guard(void) {}
static inline void risc_sd_spi_begin_operation(void) {}
static inline void risc_sd_spi_end_operation(void) {}
#endif
#ifdef __cplusplus
class SdSpiOperation {
 public:
  explicit SdSpiOperation(bool affected = true) : affected_(affected) {
    if (affected_) risc_sd_spi_begin_operation();
  }
  ~SdSpiOperation() {
    if (affected_) risc_sd_spi_end_operation();
  }
  SdSpiOperation(const SdSpiOperation&) = delete;
  SdSpiOperation& operator=(const SdSpiOperation&) = delete;

 private:
  bool affected_;
};
#endif
