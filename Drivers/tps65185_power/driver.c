/* T5 TPS65185 single register owner and EPD power sequence. Owns only its
 * PCA display pins. It never handles button or radio rail registers. */
#include "RiscDisplayPowerV1.h"
#include "RiscGpioExpanderV1.h"
#include "RiscI2cBusV1.h"
#include "RiscPlatformClockV1.h"
#include <string.h>
static const risc_i2c_bus_api_v1 *bus;
static const risc_gpio_expander_api_v1 *gpio;
static const risc_platform_clock_api_v1 *clock_api;
static uint64_t chip,pins,lease,serial;
static bool active,busy,faulted;
#define OUTPUTS 0x3b00u
#define INPUTS 0xc000u
#define WAKEUP 0x2000u
static bool enter(void) {
    if(!clock_api) return false;
    uint64_t began=clock_api->monotonic_ms(clock_api->context);
    for(unsigned i=0;i<50;++i){
        if(!__atomic_test_and_set(&busy,__ATOMIC_ACQUIRE))return true;
        if(clock_api->monotonic_ms(clock_api->context)-began>=50)return false;
        clock_api->sleep_ms(clock_api->context,1);
    }
    return false;
}
static void leave(void){__atomic_clear(&busy,__ATOMIC_RELEASE);}
static bool outputs(uint16_t mask,uint16_t value){
    bool okay=gpio->write(gpio->context,pins,mask,value);
    if(!okay)faulted=true;
    return okay;
}
static bool write_register(uint8_t reg,const uint8_t*data,size_t n){
    if(n>2)return false;
    uint8_t bytes[3]={reg,0,0};memcpy(bytes+1,data,n);
    bool okay=bus->transact(bus->context,chip,bytes,n+1,NULL,0,50);
    if(!okay)faulted=true;
    return okay;
}
static bool ready(bool internal){
    const uint64_t began=clock_api->monotonic_ms(clock_api->context);
    // Both work and elapsed bounds; every unsuccessful read cooperates.
    for(unsigned i=0;i<400;++i){
        bool okay;
        if(internal){
            const uint8_t reg=0x0f;uint8_t value=0;
            okay=bus->transact(bus->context,chip,&reg,1,&value,1,50) && (value&0xfa)==0xfa;
        }else{
            uint16_t value=0;
            okay=gpio->read(gpio->context,pins,&value) && (value&0x4000);
        }
        if(okay)return true;
        if(clock_api->monotonic_ms(clock_api->context)-began>=400)return false;
        clock_api->sleep_ms(clock_api->context,1);
    }
    return false;
}
static bool power_off(void){
    // Preserve the established OE/mode/PWRUP/VCOM-before-WAKEUP order.
    if(!outputs(OUTPUTS&~WAKEUP,0))return false;
    clock_api->sleep_ms(clock_api->context,1);
    return outputs(WAKEUP,0);
}
static bool acquire(void*ctx,uint64_t*out){
    (void)ctx;if(out)*out=0;
    if(!out||!enter())return false;
    bool okay=active&&!faulted&&!lease&&serial!=UINT64_MAX;
    if(okay){
        lease=++serial;*out=lease; // Pin partial power-up before any write.
        okay=outputs(OUTPUTS,OUTPUTS);
        if(okay){clock_api->sleep_ms(clock_api->context,1);okay=ready(false);}
        const uint8_t enable=0x3f,vcom[2]={160,0}; // Existing -1600mV profile.
        if(okay)okay=write_register(0x01,&enable,1);
        if(okay)okay=write_register(0x03,vcom,2);
        if(okay)okay=ready(true);
        // Read timeout can recover through confirmed safe outputs. A failed
        // write remains quarantined even if the subsequent shutdown succeeds.
        if(!okay){
            bool off=power_off();
            if(off&&!faulted){lease=0;*out=0;}
        }
    }
    leave();return okay;
}
static bool release(void*ctx,uint64_t token){
    (void)ctx;if(!enter())return false;
    bool okay=active&&token&&token==lease;
    if(okay)okay=power_off()&&!faulted;
    if(okay)lease=0;
    leave();return okay;
}
static bool start(const risc_provider_dependency_v1*deps,size_t count){
    if(active||chip||pins||lease||(count&&!deps))return false;
    bus=NULL;gpio=NULL;clock_api=NULL;
    for(size_t i=0;i<count;++i){
        if(deps[i].api_version!=1||!deps[i].capability_id)continue;
        if(!strcmp(deps[i].capability_id,"i2c.bus"))bus=deps[i].api;
        if(!strcmp(deps[i].capability_id,"gpio.expander"))gpio=deps[i].api;
        if(!strcmp(deps[i].capability_id,"platform.clock"))clock_api=deps[i].api;
    }
    if(!bus||bus->api_version!=1||bus->struct_size<sizeof(*bus)||!bus->claim_device||!bus->release_device||!bus->transact||
       !gpio||gpio->api_version!=1||gpio->struct_size<sizeof(*gpio)||!gpio->claim||!gpio->read||!gpio->write||!gpio->release||
       !clock_api||clock_api->api_version!=1||clock_api->struct_size<sizeof(*clock_api)||!clock_api->monotonic_ms||!clock_api->sleep_ms)return false;
    if(!bus->claim_device(bus->context,0x68,&chip))return false;
    if(!gpio->claim(gpio->context,OUTPUTS|INPUTS,INPUTS,0,&pins))return false;
    active=true;return true;
}
static bool quiesce(void){
    if(!chip&&!pins)return !active;
    if(!enter())return false;
    bool okay=!lease&&!faulted;
    if(okay)active=false; // No new power admission after partial teardown.
    if(okay&&pins){okay=gpio->release(gpio->context,pins);if(okay)pins=0;}
    if(okay&&chip){okay=bus->release_device(bus->context,chip);if(okay)chip=0;}
    if(okay)active=false;
    leave();return okay;
}
static void stop(void){(void)quiesce();}
static const risc_display_power_api_v1 api={1,sizeof(api),NULL,acquire,release};
static const risc_driver_v2 driver={2,sizeof(driver),"tps65185-power","display.power",1,&api,start,stop,quiesce};
__attribute__((visibility("default")))
const risc_driver_v2*t5_driver_get(uint32_t abi){return abi==2?&driver:NULL;}
