#include <cassert>
#include <cstdint>
#include <map>
#include <cstdio>
static std::map<uint32_t,uint32_t> registers;
static unsigned writes;
uint32_t x4pro_reg_read(uint32_t address) { return registers[address]; }
void x4pro_reg_write(uint32_t address,uint32_t value) { registers[address]=value;++writes; }
void x4pro_pin_prepare(uint32_t pin,bool pullup) { assert(pin==11 && !pullup); }
void x4pro_pin_output(uint32_t pin,bool level) { assert(pin==11 && !level); }
uint32_t x4pro_enable_w1ts(uint32_t pin) { assert(pin==11);return 0x60004024; }
uint32_t x4pro_pin_mask(uint32_t pin) { return 1u<<pin; }
#include "../../Drivers/t5s3_frontlight/driver.c"
int main() {
  const auto* provider=t5_driver_get(2);assert(provider);
  const auto* light=static_cast<const risc_frontlight_api_v1*>(provider->capability);
  assert(provider->start(nullptr,0));assert(!provider->start(nullptr,0));
  assert((registers[CLOCK_SELECT]&3)==3);
  assert(registers[TIMER0]==((8000u<<4)|8u|(1u<<25)));
  assert(registers[X4PRO_GPIO_MATRIX_BASE+44]==73);
  assert(registers[LEDC_BASE+8]==0);
  for(unsigned i=0;i<=10;++i) {
    assert(light->set_level(nullptr,i,10));
    unsigned duty=(i*i*255+50)/100;if(duty==255)duty=256;
    assert(registers[LEDC_BASE+8]==duty*16);
    uint16_t level,max;assert(light->get_level(nullptr,&level,&max));assert(level==i && max==10);
  }
  assert(!light->set_level(nullptr,11,10));
  assert(provider->quiesce());assert(!light->set_level(nullptr,1,10));
  registers[CLOCK_SELECT]=1;assert(provider->start(nullptr,0));
  assert(registers[TIMER0]==((16000u<<4)|8u|(1u<<25)));assert(provider->quiesce());
  registers[CLOCK_SELECT]=2;unsigned before=writes;
  assert(!provider->start(nullptr,0));assert(writes==before);
  registers[CLOCK_SELECT]=3;registers[LEDC_BASE+0x14]=4;
  assert(!provider->start(nullptr,0));assert(writes==before);
  registers[LEDC_BASE+0x14]=5; // Other active channel uses another timer.
  assert(provider->start(nullptr,0));assert(registers[LEDC_BASE+0x14]==5);
  assert(provider->quiesce());
  puts("T5 ELF frontlight: duty curve, clock selection, shared-timer conflict, lifecycle PASS");
}
