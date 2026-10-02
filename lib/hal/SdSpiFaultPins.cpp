#include "SdSpiFault.h"
#if defined(RISCRTE_SD_SPI_FAULT_PORT)
#include <Arduino.h>
#include <Board.h>
#include <driver/gpio.h>

namespace {
bool affected(uint8_t pin) {
#if defined(BOARD_T5S3_PRO) || defined(BOARD_T5S3)
  return pin == T5S3_SPI_MISO || pin == T5S3_SPI_MOSI || pin == T5S3_SPI_SCLK || pin == T5S3_SD_CS ||
         pin == T5S3_LORA_CS || pin == T5S3_LORA_RST || pin == T5S3_LORA_IRQ || pin == T5S3_LORA_BUSY;
#elif defined(BOARD_LILYGO_EPD47_S3)
  return pin == EPD47_SD_MISO || pin == EPD47_SD_MOSI || pin == EPD47_SD_SCLK || pin == EPD47_SD_CS;
#else
#error SD/SPI fault pin boundary requires an explicit board profile
#endif
}
void guard(uint8_t pin) {
  if (risc_sd_spi_faulted() && affected(pin)) risc_sd_spi_guard();
}
}  // namespace
// Also cover ordinary raw compatibility imports. No pin/rail reset can race
// the retained bus after failure; unrelated pins keep their normal behavior.
extern "C" void __real_pinMode(uint8_t, uint8_t);
extern "C" void __wrap_pinMode(uint8_t pin, uint8_t mode) {
  guard(pin);
  __real_pinMode(pin, mode);
}
extern "C" void __real_digitalWrite(uint8_t, uint8_t);
extern "C" void __wrap_digitalWrite(uint8_t pin, uint8_t value) {
  guard(pin);
  __real_digitalWrite(pin, value);
}
extern "C" esp_err_t __real_gpio_set_level(gpio_num_t, uint32_t);
extern "C" esp_err_t __wrap_gpio_set_level(gpio_num_t pin, uint32_t value) {
  guard(static_cast<uint8_t>(pin));
  return __real_gpio_set_level(pin, value);
}
extern "C" esp_err_t __real_gpio_config(const gpio_config_t* config);
extern "C" esp_err_t __wrap_gpio_config(const gpio_config_t* config) {
  if (config && risc_sd_spi_faulted())
    for (uint8_t pin = 0; pin < 64; ++pin)
      if (config->pin_bit_mask & (UINT64_C(1) << pin)) guard(pin);
  return __real_gpio_config(config);
}
#endif
