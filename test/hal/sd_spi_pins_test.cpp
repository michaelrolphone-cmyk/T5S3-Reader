#include "SdSpiFault.h"
#include <Board.h>
#include <driver/gpio.h>
#include <cassert>
#include <csetjmp>
#include <cstdio>
static bool fault;
static unsigned writes;
static std::jmp_buf parked;
extern "C" bool risc_sd_spi_faulted() { return fault; }
extern "C" void risc_sd_spi_guard() { assert(fault); std::longjmp(parked,1); }
extern "C" void __real_pinMode(uint8_t,uint8_t) { ++writes; }
extern "C" void __real_digitalWrite(uint8_t,uint8_t) { ++writes; }
extern "C" esp_err_t __real_gpio_set_level(gpio_num_t,uint32_t) { ++writes; return 0; }
extern "C" esp_err_t __real_gpio_config(const gpio_config_t*) { ++writes; return 0; }
extern "C" void __wrap_pinMode(uint8_t,uint8_t);
extern "C" void __wrap_digitalWrite(uint8_t,uint8_t);
extern "C" esp_err_t __wrap_gpio_set_level(gpio_num_t,uint32_t);
extern "C" esp_err_t __wrap_gpio_config(const gpio_config_t*);
int main() {
#if defined(BOARD_T5S3_PRO)
 const uint8_t pins[]={T5S3_SPI_MISO,T5S3_SPI_MOSI,T5S3_SPI_SCLK,T5S3_SD_CS,T5S3_LORA_CS,T5S3_LORA_RST,T5S3_LORA_IRQ,T5S3_LORA_BUSY};
#else
 const uint8_t pins[]={EPD47_SD_MISO,EPD47_SD_MOSI,EPD47_SD_SCLK,EPD47_SD_CS};
#endif
 for(auto pin:pins) { __wrap_pinMode(pin,1); __wrap_digitalWrite(pin,1); }
 fault=true;
 for(auto pin:pins) {
  const auto before=writes;
  if (!setjmp(parked)) { __wrap_pinMode(pin,1); assert(false); }
  if (!setjmp(parked)) { __wrap_digitalWrite(pin,0); assert(false); }
  if (!setjmp(parked)) { __wrap_gpio_set_level(pin,0); assert(false); }
  gpio_config_t config{UINT64_C(1)<<pin};
  if (!setjmp(parked)) { __wrap_gpio_config(&config); assert(false); }
  assert(writes==before);
 }
 const auto before=writes;
 __wrap_pinMode(2,1); __wrap_digitalWrite(2,1); __wrap_gpio_set_level(2,1);
 gpio_config_t config{UINT64_C(1)<<2}; __wrap_gpio_config(&config);
 assert(writes==before+4);
 puts("Actual board pin wrappers: affected SPI/SD/LoRa cleanup blocked, unrelated pins preserved PASS");
}
