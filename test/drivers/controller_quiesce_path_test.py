#!/usr/bin/env python3
"""Execute the unchanged production quiesce_host body against IDF fault ports.

This is cleanup ordering/retention evidence, not USB electrical execution.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'Drivers/usb_controller_esp32s3/driver_base.cpp').read_text()
body = 'bool quiesce_host() {' + source.split('bool quiesce_host() {', 1)[1].split('bool quiesce(void *)', 1)[0]
preamble = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include "RiscUsbVbusV1.h"
using esp_err_t = int;
using TickType_t = uint32_t;
constexpr int ESP_OK=0, ESP_ERR_TIMEOUT=1, ESP_ERR_NOT_FINISHED=2, ERROR=3;
constexpr unsigned USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS=1, USB_HOST_LIB_EVENT_FLAGS_ALL_FREE=2;
constexpr unsigned kTeardownTicks=8;
struct Claim { uint64_t token; } claims[2];
struct Device { void *handle; } devices[2];
static bool inFlight, fault, installed, running, noClientsObserved;
static void *client, *transfer, *phy;
static uint64_t powerLease;
static unsigned queueHead, queueTail, queueCount;
static uint32_t ticks;
static int fail, stage, monitorStatus;
static unsigned restored, powerReleases, eventCalls, freeCalls, yields;
struct { struct { bool sw_hw_usb_phy_sel, sw_usb_phy_sel; } usb_conf; } RTCCNTL;
#include "PhyRoute.h"
static bool operation(int n) { stage=n; return fail!=n; }
static bool drain_bulk(bool) { if (!operation(1)) return false; inFlight=false; return true; }
static bool claimed(uint64_t) { return fail==2; }
static esp_err_t usb_host_device_close(void *, void *) { return operation(3)?ESP_OK:ERROR; }
static esp_err_t usb_host_transfer_free(void *) {
    assert(!inFlight && !devices[0].handle && !devices[1].handle);
    return operation(4)?ESP_OK:ERROR;
}
static esp_err_t usb_host_client_deregister(void *) { assert(!transfer); return operation(5)?ESP_OK:ERROR; }
static TickType_t xTaskGetTickCount() { return ticks; }
static TickType_t pdMS_TO_TICKS(unsigned n) { return n; }
static void vTaskDelay(unsigned n) { ticks+=n; ++yields; }
static esp_err_t usb_host_lib_handle_events(unsigned, uint32_t *flags) {
    ++eventCalls;
    const int n=noClientsObserved?8:6;
    if (!operation(n)) return ERROR;
    *flags=(fail==12 || (fail==13 && noClientsObserved))?0:(noClientsObserved?USB_HOST_LIB_EVENT_FLAGS_ALL_FREE:USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS);
    return ESP_OK;
}
static esp_err_t usb_host_device_free_all() {
    assert(!client && noClientsObserved); ++freeCalls;
    if (!operation(7)) return ERROR;
    return ESP_ERR_NOT_FINISHED;
}
static esp_err_t usb_host_uninstall() { return operation(9)?ESP_OK:ERROR; }
static bool release_host_phy() {
    if (!phy) return true;
    assert(!installed && !client && !transfer);
    if (!operation(10)) return false;
    phy=nullptr; return true;
}
static bool release_power(void *, uint64_t token) {
    assert(token==42 && !phy && !installed && phyRouteCaptured); ++powerReleases;
    return operation(11);
}
static int32_t input_status(void *) { assert(!powerLease && !phy); return monitorStatus; }
static risc_usb_vbus_api_v1 powerApi = {RISC_USB_VBUS_API_V1, sizeof(powerApi), nullptr, nullptr, release_power, nullptr};
static risc_usb_vbus_monitor_api_v1 monitorApi = {powerApi, input_status, 0};
static const risc_usb_vbus_api_v1 *power=&powerApi;
static const risc_usb_vbus_monitor_api_v1 *powerMonitor=&monitorApi;
'''
# Keep the production register restoration while observing its call boundary.
preamble += '\nstatic void checked_restore() {\n assert(!phy && !installed && !client && !transfer && !powerLease && !inFlight);\n ++restored; restore_phy_route();\n}\n#define restore_phy_route checked_restore\n'
main = r'''
#undef restore_phy_route
static void reset() {
    inFlight=installed=running=true; fault=false; noClientsObserved=false;
    client=transfer=phy=&ticks; devices[0].handle=&ticks; devices[1].handle=&ticks;
    claims[0]={}; claims[1]={}; powerLease=42;
    queueHead=queueTail=queueCount=1; ticks=0; stage=0;
    restored=powerReleases=eventCalls=freeCalls=yields=0;
    monitorStatus=RISC_USB_POWER_ABSENT;
    RTCCNTL.usb_conf={true,false}; capture_phy_route(); RTCCNTL.usb_conf={true,true};
}
static void complete() {
    assert(!running && !installed && !client && !transfer && !phy && !powerLease);
    assert(!phyRouteCaptured && !RTCCNTL.usb_conf.sw_usb_phy_sel);
    assert(restored==1 && !queueHead && !queueTail && !queueCount);
}
int main() {
    for (int n=1;n<=13;++n) {
        reset(); fail=n;
        assert(!quiesce_host());
        assert(!restored && running && phyRouteCaptured);
        assert(RTCCNTL.usb_conf.sw_usb_phy_sel && queueCount==1);
        if(n<=4) assert(transfer);
        if(n<=5) assert(client);
        if(n<=9) assert(installed);
        if(n<=10) assert(phy);
        assert(powerLease==42);
        if(n==12) assert(eventCalls<=kTeardownTicks && yields==kTeardownTicks);
        if(n==13) assert(noClientsObserved && installed && eventCalls<=kTeardownTicks+1);
        fail=0; assert(quiesce_host()); complete();
    }
    reset(); fail=0; claims[0].token=7;
    assert(!quiesce_host() && transfer && !restored);
    claims[0].token=0; assert(quiesce_host()); complete();
    for(int status : {RISC_USB_POWER_UNKNOWN, RISC_USB_POWER_SOURCE}) {
        reset(); monitorStatus=status;
        assert(!quiesce_host() && powerLease==0 && !phy && phyRouteCaptured && !restored);
        auto released=powerReleases;
        monitorStatus=RISC_USB_POWER_EXTERNAL;
        assert(quiesce_host() && powerReleases==released); complete();
    }
    reset(); fault=true;
    assert(quiesce_host()); complete();
    reset(); ticks=UINT32_MAX-3; fail=12;
    assert(!quiesce_host() && eventCalls<=kTeardownTicks && !restored);
    fail=0; assert(quiesce_host()); complete();
    puts("Production controller teardown: failure retention, retry, bounds, source-off and PHY restore PASS");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path/'test.cpp').write_text('#include <initializer_list>\n'+preamble+body+main)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-I'+str(ROOT/'sdk/driver'), '-I'+str(ROOT/'Drivers/usb_controller_esp32s3'),
                    str(path/'test.cpp'), '-o', str(path/'test')], check=True)
    subprocess.run([str(path/'test')], check=True, timeout=10)
