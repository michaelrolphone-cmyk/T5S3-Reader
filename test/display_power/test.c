#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "RiscI2cBusV1.h"
#include "RiscGpioExpanderV1.h"
#include "RiscDisplayPowerV1.h"
#include "RiscPlatformClockV1.h"
extern const risc_driver_v2*pca_driver_get(uint32_t);
static uint8_t pca[8]={0,0x40,0,0,0,0,255,255};
static uint8_t addresses[8];static uint64_t serial,now;
static bool fail_tps,not_ready,fail_release;
static unsigned enable_writes,vcom_writes;
static uint8_t last_output;static uint64_t wake_only_at;
static uint64_t time_now(void*ctx){(void)ctx;return now;}
static void sleep_ms(void*ctx,uint32_t ms){(void)ctx;now+=ms;}
static bool claim_chip(void*ctx,uint8_t addr,uint64_t*out){
 (void)ctx;assert(addr==0x20||addr==0x68);for(unsigned i=1;i<=serial;++i)assert(addresses[i]!=addr);
 assert(serial<7);*out=++serial;addresses[serial]=addr;return true;
}
static bool release_chip(void*ctx,uint64_t token){
 (void)ctx;assert(token&&token<=serial&&addresses[token]);if(fail_release)return false;addresses[token]=0;return true;
}
static bool transfer(void*ctx,uint64_t token,const uint8_t*w,size_t wn,uint8_t*r,size_t rn,uint32_t budget){
 (void)ctx;assert(budget==50&&token&&token<=serial);++now;
 if(addresses[token]==0x20){
  assert(wn&&w[0]<=6);
  if(wn==1&&rn==2){memcpy(r,pca+w[0],2);return true;}
  assert(wn==3&&!rn);
  if(w[0]==2){
   uint8_t outputs=w[2]&0x3b;
   if(last_output==0x3b && outputs==0x20)wake_only_at=now;
   if(last_output==0x20 && outputs==0)assert(now>wake_only_at);
   last_output=outputs;
  }
  memcpy(pca+w[0],w+1,2);return true;
 }
 assert(addresses[token]==0x68);
 if(wn==1&&rn==1){assert(w[0]==0xf);*r=not_ready?0:0xfa;return true;}
 assert(!rn);
 if(w[0]==1){assert(wn==2&&w[1]==0x3f);++enable_writes;}
 else {assert(w[0]==3&&wn==3&&w[1]==160&&w[2]==0);++vcom_writes;}
 return !fail_tps;
}
int main(int argc,char**argv){
 const risc_i2c_bus_api_v1 bus={1,sizeof(bus),NULL,claim_chip,transfer,release_chip};
 const risc_platform_clock_api_v1 clock={1,sizeof(clock),NULL,time_now,sleep_ms};
 const risc_provider_dependency_v1 pdeps[]={{"i2c.bus",1,&bus},{"platform.clock",1,&clock}};
 const risc_driver_v2*pdriver=pca_driver_get(2);assert(pdriver->start(pdeps,2));
 const risc_gpio_expander_api_v1*gpio=pdriver->capability;
 uint64_t radio,button;assert(gpio->claim(NULL,1,0,0,&radio));assert(gpio->write(NULL,radio,1,1));
 assert(gpio->claim(NULL,0x400,0x400,0,&button));
 const risc_provider_dependency_v1 deps[]={{"i2c.bus",1,&bus},{"gpio.expander",1,gpio},{"platform.clock",1,&clock}};
 const risc_driver_v2*driver=t5_driver_get(2);assert(driver->start(deps,3));
 const risc_display_power_api_v1*power=driver->capability;
 uint64_t lease=0,collision=99;
 if(argc>1&&!strcmp(argv[1],"write")){
  fail_tps=true;assert(!power->acquire(NULL,&lease)&&lease);
  assert(enable_writes==1&&!vcom_writes);
  assert(!driver->quiesce());assert(!power->release(NULL,lease));
  assert(addresses[1]==0x20&&addresses[2]==0x68);
  uint16_t levels;assert(gpio->read(NULL,button,&levels)); // Unrelated input survives.
 }else{
  if(argc>1){
   not_ready=true;uint64_t began=now;
   assert(!power->acquire(NULL,&lease)&&!lease);assert(now-began<600);
   assert((pca[3]&0x3b)==0);not_ready=false;
  }
  assert(power->acquire(NULL,&lease)&&lease);assert((pca[3]&0x3b)==0x3b);
  assert(!power->acquire(NULL,&collision)&&!collision);assert(!driver->quiesce());
  assert(!power->release(NULL,lease+1));assert(power->release(NULL,lease));
  assert((pca[3]&0x3b)==0 && (pca[2]&1)); // Display release preserves radio.
  assert(!power->release(NULL,lease));
  fail_release=true;assert(!driver->quiesce());fail_release=false;assert(driver->quiesce());
  assert(gpio->release(NULL,button));assert(gpio->release(NULL,radio));assert(pdriver->quiesce());
 }
 puts("Actual TPS + PCA: shared pins, power ordering, bounded ready timeout, stale lease and failed-write retention PASS");
}
