/* Sole PCA9535 register owner. A display, button or radio consumer receives
 * a nonoverlapping pin grant, never the underlying address/register access. */
#include "RiscGpioExpanderV1.h"
#include "RiscI2cBusV1.h"
#include "RiscPlatformClockV1.h"
#include <stddef.h>
#include <string.h>
#define MAX_GRANTS 8u
static const risc_i2c_bus_api_v1 *bus;
static const risc_platform_clock_api_v1 *clock_api;
static uint64_t claim, next_token;
static bool started, busy, uncertain;
typedef struct { uint64_t token; uint16_t pins, inputs, safe; } pin_grant;
static pin_grant grants[MAX_GRANTS];
static bool enter(void) {
    if (!clock_api) return false;
    const uint64_t began=clock_api->monotonic_ms(clock_api->context);
    for (unsigned attempt=0; attempt<50; ++attempt) {
        if (!__atomic_test_and_set(&busy,__ATOMIC_ACQUIRE)) return true;
        if (clock_api->monotonic_ms(clock_api->context)-began>=50) return false;
        clock_api->sleep_ms(clock_api->context,1);
    }
    return false;
}
static void leave(void) { __atomic_clear(&busy,__ATOMIC_RELEASE); }
static pin_grant *find(uint64_t token) {
    for (unsigned i=0;token && i<MAX_GRANTS;++i)
        if(grants[i].token==token) return &grants[i];
    return NULL;
}
static bool read_register(uint8_t reg,uint16_t *value) {
    uint8_t data[2];
    if (!bus->transact(bus->context,claim,&reg,1,data,2,50)) return false;
    *value=(uint16_t)(data[0]|((uint16_t)data[1]<<8)); return true;
}
/* Caller owns serialization across the entire RMW / claim / release. */
static bool modify(uint8_t reg,uint16_t mask,uint16_t bits) {
    if (!mask) return true;
    uint16_t old;
    if (!read_register(reg,&old)) return false;
    const uint16_t next=(old&~mask)|(bits&mask);
    if (next==old) return true;
    uint8_t data[3]={reg,(uint8_t)next,(uint8_t)(next>>8)};
    bool okay=bus->transact(bus->context,claim,data,3,NULL,0,50);
    if (!okay) uncertain=true; // Never retry an unconfirmed physical write.
    return okay;
}
static bool claim_pins(void *context,uint16_t pins,uint16_t inputs,uint16_t safe,uint64_t *out) {
    (void)context;
    if(out) *out=0;
    if(!out || !pins || (inputs&~pins) || (safe&~(pins&~inputs)) || !enter()) return false;
    pin_grant *free_slot=NULL;
    bool okay=started && !uncertain && next_token!=UINT64_MAX;
    for(unsigned i=0;i<MAX_GRANTS;++i) {
        if(grants[i].token && (grants[i].pins&pins)) okay=false;
        if(!grants[i].token && !free_slot) free_slot=&grants[i];
    }
    if(okay && free_slot) {
        *free_slot=(pin_grant){++next_token,pins,inputs,safe};
        // Retain even a failed setup: some pin changes may have completed.
        *out=free_slot->token;
        okay=modify(2,pins&~inputs,safe) && modify(6,pins,inputs);
    } else okay=false;
    leave();return okay;
}
static bool read_levels(void *context,uint64_t token,uint16_t *value) {
    (void)context;
    if (!value || !enter()) return false;
    pin_grant *grant=find(token); uint16_t levels;
    bool okay=started && !uncertain && grant && read_register(0,&levels);
    if(okay) *value=levels&grant->pins;
    leave(); return okay;
}
static bool write_levels(void *context,uint64_t token,uint16_t mask,uint16_t bits) {
    (void)context;
    if(!mask || !enter()) return false;
    pin_grant *grant=find(token);
    bool okay=started && !uncertain && grant && !(mask&~(grant->pins&~grant->inputs));
    if(okay) okay=modify(2,mask,bits);
    leave();return okay;
}
static bool release_pins(void *context,uint64_t token) {
    (void)context;
    if(!enter()) return false;
    pin_grant *grant=find(token);
    bool okay=started && !uncertain && grant;
    if(okay) okay=modify(2,grant->pins&~grant->inputs,grant->safe);
    if(okay) memset(grant,0,sizeof(*grant));
    leave();return okay;
}
static bool start(const risc_provider_dependency_v1 *deps,size_t count) {
    if (started || claim || (count && !deps)) return false;
    bus=NULL;clock_api=NULL;
    for (size_t i=0;i<count;++i) {
        if (deps[i].api_version!=1 || !deps[i].capability_id) continue;
        if (!strcmp(deps[i].capability_id,"i2c.bus")) bus=deps[i].api;
        if (!strcmp(deps[i].capability_id,"platform.clock")) clock_api=deps[i].api;
    }
    if (!bus || bus->api_version!=1 || bus->struct_size<sizeof(*bus) ||
        !bus->claim_device || !bus->transact || !bus->release_device ||
        !clock_api || clock_api->api_version!=1 || clock_api->struct_size<sizeof(*clock_api) ||
        !clock_api->monotonic_ms || !clock_api->sleep_ms) { bus=NULL;clock_api=NULL;return false; }
    if (!bus->claim_device(bus->context,0x20,&claim)) return false;
    uint16_t probe;
    if (!read_register(6,&probe)) return false;
    started=true;return true;
}
static bool quiesce(void) {
    if (!claim) return !started;
    if (!enter()) return false;
    bool safe=!uncertain;
    for(unsigned i=0;i<MAX_GRANTS;++i) if(grants[i].token) safe=false;
    if(safe) safe=bus->release_device(bus->context,claim);
    if (safe) { claim=0;started=false; }
    leave();return safe;
}
static void stop(void) { (void)quiesce(); }
static const risc_gpio_expander_api_v1 api={1,sizeof(api),NULL,claim_pins,read_levels,write_levels,release_pins};
static const risc_driver_v2 driver={2,sizeof(driver),"pca9535-gpio","gpio.expander",1,&api,start,stop,quiesce};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) { return abi==2?&driver:NULL; }
