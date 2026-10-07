#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "RiscI2cBusV1.h"
#include "RiscPlatformClockV1.h"
#include "RiscGpioExpanderV1.h"
static uint8_t registers[8];
static uint64_t now, generation;
static bool claimed,fail_write,fail_release;
static unsigned transactions;
static uint64_t clock_now(void *ctx){(void)ctx;return now;}
static void clock_sleep(void *ctx,uint32_t ms){(void)ctx;now+=ms;}
static bool claim_chip(void *ctx,uint8_t address,uint64_t*out){
 (void)ctx;assert(address==0x20);if(claimed)return false;claimed=true;*out=++generation;return true;
}
static bool transact(void*ctx,uint64_t token,const uint8_t*w,size_t wn,uint8_t*r,size_t rn,uint32_t ms){
 (void)ctx;assert(claimed&&token==generation&&ms==50);++transactions;
 if(wn==1&&rn==2){assert(w[0]<=6);memcpy(r,registers+w[0],2);return true;}
 assert(wn==3&&!rn&&(w[0]==2||w[0]==6));
 if(fail_write)return false;
 memcpy(registers+w[0],w+1,2);return true;
}
static bool release_chip(void*ctx,uint64_t token){(void)ctx;assert(claimed&&token==generation);if(fail_release)return false;claimed=false;return true;}
int main(int argc,char**argv){
 (void)argv;
 const risc_i2c_bus_api_v1 bus={1,sizeof(bus),NULL,claim_chip,transact,release_chip};
 const risc_platform_clock_api_v1 clock={1,sizeof(clock),NULL,clock_now,clock_sleep};
 const risc_provider_dependency_v1 deps[]={{"i2c.bus",1,&bus},{"platform.clock",1,&clock}};
 const risc_driver_v2*driver=t5_driver_get(2);assert(driver);
 const risc_gpio_expander_api_v1*api=driver->capability;
 memset(registers,0xa5,sizeof(registers));
 assert(!driver->start(NULL,0));assert(driver->quiesce());
 assert(driver->start(deps,2));assert(!driver->start(deps,2));
 uint64_t panel=0,button=0,radio=0,collision=123;
 assert(api->claim(NULL,0xfb00,0xc000,0,&panel));
 assert(api->claim(NULL,0x0400,0x0400,0,&button));
 assert(api->claim(NULL,1,0,0,&radio));
 assert(!api->claim(NULL,0x0100,0,0,&collision)&&!collision);
 unsigned before=transactions;
 assert(!api->write(NULL,button,0x0400,0));
 assert(!api->write(NULL,panel,1,1));assert(transactions==before);
 assert(api->write(NULL,radio,1,1));
 assert(api->write(NULL,panel,0x0300,0x0200));
 assert(registers[2]&1);assert((registers[3]&3)==2);
 assert((registers[7]&0xc4)==0xc4); // Button and power-good stay inputs.
 uint16_t value;assert(api->read(NULL,button,&value)&&value==0x0400);
 assert(!driver->quiesce());
 if(argc>1){
  fail_write=true;assert(!api->write(NULL,panel,0x0100,0x0100));before=transactions;
  assert(!api->read(NULL,button,&value));assert(!api->write(NULL,radio,1,0));
  assert(!api->release(NULL,panel));assert(!driver->quiesce());
  assert(claimed&&transactions==before);
 }else{
  assert(api->release(NULL,panel));assert((registers[3]&0x3b)==0);
  assert(registers[2]&1); // Releasing display does not drop radio rail.
  assert(!api->read(NULL,panel,&value));assert(!api->release(NULL,panel));
  assert(api->release(NULL,button));assert(api->release(NULL,radio));
  fail_release=true;assert(!driver->quiesce());assert(claimed);
  fail_release=false;assert(driver->quiesce());assert(!claimed);
  assert(driver->start(deps,2));assert(generation==2);
  uint64_t fresh;assert(api->claim(NULL,1,0,0,&fresh)&&fresh!=radio);
  assert(!api->write(NULL,radio,1,1));assert(api->release(NULL,fresh));
  assert(driver->quiesce());
 }
 puts("PCA9535 ELF: scoped grants, masked preservation, stale rejection, uncertainty retention PASS");
}
