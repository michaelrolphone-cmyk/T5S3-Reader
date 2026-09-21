#include "RiscUsbControllerV1.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

/* Two independently claimed interfaces on one composite physical device.
 * Exercise the production host ELF, not a copy of its authorization logic. */
static const uint8_t config[] = {
    9,2,55,0,2,1,0,0x80,50,
    9,4,0,0,2,0xff,0,0,0,
    7,5,0x81,2,64,0,0,7,5,0x02,2,64,0,0,
    9,4,1,0,2,0xff,0,0,0,
    7,5,0x83,2,64,0,0,7,5,0x04,2,64,0,0
};
static bool pending, release_fails;
static unsigned controls, releases;
static risc_usb_controller_event_v1 event;
static int32_t next_event(void *ctx, risc_usb_controller_event_v1 *out) {
    (void)ctx;
    if (!pending) return 0;
    *out = event;
    pending = false;
    return 1;
}
static bool configuration(void *ctx, uint64_t dev, uint8_t *out, size_t *size,
                          uint16_t *vid, uint16_t *pid) {
    (void)ctx;
    if (dev != 42 || !out || !size || *size < sizeof(config)) return false;
    memcpy(out, config, sizeof(config));
    *size = sizeof(config);
    *vid = 0x1234; *pid = 0xabcd;
    return true;
}
static bool claim(void *ctx, uint64_t dev, uint8_t iface, uint8_t alt,
                  uint64_t *out) {
    (void)ctx;
    if (dev != 42 || iface > 1 || alt || !out) return false;
    *out = (uint64_t)(501 + iface);
    return true;
}
static bool release(void *ctx, uint64_t claim_token) {
    (void)ctx;
    assert(claim_token == 501 || claim_token == 502);
    ++releases;
    return !release_fails;
}
static int32_t control(void *ctx, uint64_t dev, uint8_t type, uint8_t req,
                       uint16_t value, uint16_t index, uint8_t *data,
                       uint16_t length, uint32_t timeout) {
    (void)ctx; (void)req; (void)value; (void)index; (void)data;
    assert(dev == 42 && timeout == 1000 &&
           ((type & 0x1fu) == 0u || (type & 0x1fu) == 1u));
    ++controls;
    return length;
}
static int32_t bulk_read(void *ctx, uint64_t claim_token, uint8_t ep,
                         uint8_t *out, size_t cap, uint32_t timeout) {
    (void)ctx; (void)claim_token; (void)ep; (void)out; (void)cap; (void)timeout;
    return -1;
}
static int32_t bulk_write(void *ctx, uint64_t claim_token, uint8_t ep,
                          const uint8_t *src, size_t len, uint32_t timeout) {
    (void)ctx; (void)claim_token; (void)ep; (void)src; (void)len; (void)timeout;
    return -1;
}
static bool quiesce(void *ctx) { (void)ctx; return true; }
int main(int argc, char **argv) {
    assert(argc == 2);
    void *module = dlopen(argv[1], RTLD_NOW);
    assert(module);
    risc_driver_get_v2_fn get = (risc_driver_get_v2_fn)dlsym(module, "t5_driver_get");
    assert(get);
    const risc_driver_v2 *driver = get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(driver && driver->capability);
    const risc_usb_host_discovery_v1 *host =
        (const risc_usb_host_discovery_v1 *)driver->capability;
    assert(host->host.struct_size >= sizeof(*host) && host->control_claim);
    risc_usb_controller_api_v1 controller = {
        RISC_USB_CONTROLLER_API_V1, sizeof(controller), 0,
        next_event, configuration, claim, release, control,
        bulk_read, bulk_write, quiesce
    };
    risc_provider_dependency_v1 dep = {"usb.controller", 1, &controller};
    assert(driver->start(&dep, 1));
    event = (risc_usb_controller_event_v1){1,42}; pending = true;
    size_t processed = 0;
    assert(host->poll(host->host.context, 8, &processed) && processed == 1);
    uint64_t devices[8] = {0}; size_t count = 8;
    assert(host->devices(host->host.context, devices, &count) && count == 1);
    const uint64_t dev = devices[0];
    uint64_t first = 0, second = 0;
    /* A public device ID cannot issue unowned controls. */
    assert(host->host.control(host->host.context, dev, 0x40, 1, 0, 0,
                              0, 0, 1000) < 0);
    assert(host->control_claim(host->host.context, dev, 0x40, 1, 0, 0,
                               0, 0, 1000) < 0);
    assert(host->host.claim(host->host.context, dev, 0, 0, &first) && first);
    assert(host->control_claim(host->host.context, first, 0x41, 1, 0, 0,
                               0, 0, 1000) == 0);
    assert(host->control_claim(host->host.context, first, 0x41, 1, 0, 1,
                               0, 0, 1000) < 0);
    assert(host->control_claim(host->host.context, first, 0x42, 1, 0, 0,
                               0, 0, 1000) < 0);
    /* CH34x device-recipient operations are permitted only while exclusive. */
    assert(host->control_claim(host->host.context, first, 0x40, 1, 0, 0,
                               0, 0, 1000) == 0);
    assert(host->host.claim(host->host.context, dev, 1, 0, &second) && second);
    assert(host->control_claim(host->host.context, first, 0x40, 1, 0, 0,
                               0, 0, 1000) < 0);
    assert(host->control_claim(host->host.context, second, 0x41, 1, 0, 0,
                               0, 0, 1000) < 0);
    assert(host->control_claim(host->host.context, second, 0x41, 1, 0, 1,
                               0, 0, 1000) == 0);
    assert(host->release_checked(host->host.context, second));
    assert(host->control_claim(host->host.context, second, 0x41, 1, 0, 1,
                               0, 0, 1000) < 0);
    assert(host->control_claim(host->host.context, first, 0x40, 1, 0, 0,
                               0, 0, 1000) == 0);
    release_fails = true;
    assert(!host->release_checked(host->host.context, first));
    assert(host->control_claim(host->host.context, first, 0x41, 1, 0, 0,
                               0, 0, 1000) < 0);
    release_fails = false;
    assert(host->release_checked(host->host.context, first));
    assert(host->control_claim(host->host.context, first, 0x40, 1, 0, 0,
                               0, 0, 1000) < 0);
    assert(controls == 4 && releases == 3);
    assert(driver->quiesce());
    driver->stop();
    assert(dlclose(module) == 0);
    puts("USB host composite interface isolation, exclusive vendor control and stale claims: PASS");
    return 0;
}
