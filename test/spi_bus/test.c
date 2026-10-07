/* Real bus ELF with deterministic OS/raw-port fixtures. Not hardware timing. */
#include "RiscSpiBusV1.h"
#include "RiscFirmwareSpiCompatV1.h"
#include <freertos/task.h>
#include <assert.h>
#include <stdio.h>
TickType_t fixture_ticks;
TaskHandle_t fixture_task=(void*)1;
static bool raw_active, fail_transfer, fail_end;
static unsigned raw_calls;
bool risc_fw_spi_begin_v1(uint8_t cs,uint32_t hz,bool selected) {
    (void)selected;
    assert(cs==12);
    if(raw_active || hz<100000 || hz>25000000) return false;
    raw_active=true; return true;
}
bool risc_fw_spi_select_v1(bool selected) { (void)selected; assert(raw_active); return true; }
bool risc_fw_spi_transfer_v1(const uint8_t*tx,uint8_t*rx,size_t bytes) {
    (void)tx;(void)rx;
    assert(raw_active && bytes && bytes<=4096);
    ++raw_calls; return !fail_transfer;
}
bool risc_fw_spi_end_v1(void) {
    assert(raw_active);
    if(fail_end)return false;
    raw_active=false;return true;
}
int main(void) {
    const risc_driver_v2 *driver=t5_driver_get(2);
    assert(driver && !t5_driver_get(1));
    const risc_spi_bus_api_v1 *api=driver->capability;
    assert(driver->start(NULL,0));
    uint64_t claim=0,session=0,other=99;
    assert(!api->claim_device(NULL,46,&other) && !other);
    assert(api->claim_device(NULL,12,&claim));
    assert(!api->claim_device(NULL,12,&other) && !other);
    assert(!driver->quiesce());
    assert(api->begin(NULL,claim,400000,false,&session));
    assert(!api->release_device(NULL,claim));
    assert(!driver->quiesce());
    fixture_task=(void*)2;
    assert(!api->transfer(NULL,session,NULL,NULL,1));
    assert(!api->end(NULL,session));
    fixture_task=(void*)1;
    assert(api->transfer(NULL,session,NULL,NULL,4096));
    fixture_ticks+=1000;
    unsigned before=raw_calls;
    assert(!api->transfer(NULL,session,NULL,NULL,1) && raw_calls==before);
    assert(api->end(NULL,session));
    uint64_t stale=session;
    assert(api->begin(NULL,claim,25000000,true,&session) && session!=stale);
    assert(!api->end(NULL,stale));
    fail_transfer=true;
    assert(!api->transfer(NULL,session,NULL,NULL,1));
    fail_transfer=false; before=raw_calls;
    assert(!api->transfer(NULL,session,NULL,NULL,1) && raw_calls==before);
    fail_end=true;
    assert(!api->end(NULL,session));
    assert(!api->release_device(NULL,claim) && !driver->quiesce());
    fail_end=false;
    assert(api->end(NULL,session));
    assert(api->release_device(NULL,claim));
    stale=claim;
    assert(driver->quiesce());
    assert(driver->start(NULL,0));
    assert(api->claim_device(NULL,12,&claim) && claim!=stale);
    assert(!api->begin(NULL,stale,400000,true,&session));
    assert(api->release_device(NULL,claim));
    assert(driver->quiesce());
    puts("SPI bus task ownership, stale generations, timeout and failed cleanup retention: PASS");
}
