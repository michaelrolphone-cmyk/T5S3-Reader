#include "RiscUsbProviderV1.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Representative ST-LINK/V2.1/V3 composite shape:
 * vendor debug interface plus one CDC ACM VCP function. The generic CDC
 * provider must ignore the debug interface and bind only the VCP interfaces. */
static const uint8_t descriptor[] = {
    9,2,64,0,3,1,0,0x80,50,
    9,4,0,0,2,0xff,0xff,0xff,0,
    7,5,0x81,2,64,0,0,
    7,5,0x02,2,64,0,0,
    9,4,1,0,0,2,2,1,0,
    9,4,2,0,2,10,0,0,0,
    7,5,0x83,2,64,0,0,
    7,5,0x03,2,64,0,0
};

static uint16_t pid = 0x374b;
static unsigned claims, releases, reads, writes, controls;

static bool configuration(void *ctx, uint64_t device, uint8_t *dst,
                          size_t *size, uint16_t *vid, uint16_t *out_pid) {
    (void)ctx;
    assert(device == 9 && dst && size && vid && out_pid);
    if (*size < sizeof(descriptor)) return false;
    memcpy(dst, descriptor, sizeof(descriptor));
    *size = sizeof(descriptor);
    *vid = 0x0483;
    *out_pid = pid;
    return true;
}

static bool claim(void *ctx, uint64_t device, uint8_t iface,
                  uint8_t alternate, uint64_t *lease) {
    (void)ctx;
    assert(device == 9 && alternate == 0 && lease);
    assert(iface == 1 || iface == 2);
    *lease = (uint64_t)(100 + iface);
    ++claims;
    return true;
}

static void release_claim(void *ctx, uint64_t lease) {
    (void)ctx;
    assert(lease == 101 || lease == 102);
    ++releases;
}

static int32_t control(void *ctx, uint64_t device, uint8_t type, uint8_t request,
                       uint16_t value, uint16_t iface, uint8_t *payload,
                       uint16_t length, uint32_t timeout) {
    (void)ctx; (void)value;
    assert(device == 9 && type == 0x21 && iface == 1 && timeout == 1000);
    assert((request == 0x20 && payload && length == 7) ||
           (request == 0x22 && !payload && length == 0));
    ++controls;
    return length;
}

static int32_t bulk_read(void *ctx, uint64_t lease, uint8_t ep,
                         uint8_t *dst, size_t capacity, uint32_t timeout) {
    (void)ctx;
    assert(lease == 102 && ep == 0x83 && dst && capacity >= 2 && timeout == 25);
    dst[0] = 'S'; dst[1] = 'T';
    ++reads;
    return 2;
}

static int32_t bulk_write(void *ctx, uint64_t lease, uint8_t ep,
                          const uint8_t *src, size_t length, uint32_t timeout) {
    (void)ctx;
    assert(lease == 102 && ep == 0x03 && src && length == 2 && timeout == 30);
    assert(src[0] == 'O' && src[1] == 'K');
    ++writes;
    return 2;
}

static void exercise(const risc_usb_cdc_api_v1 *cdc) {
    const uint64_t session = cdc->open(9);
    assert(session && claims == 2);
    assert(cdc->configure(session, 115200, 8, 0, 1));
    assert(cdc->control_lines(session, true, true));
    uint8_t bytes[2] = {0};
    assert(cdc->read(session, bytes, sizeof(bytes), 25) == 2);
    assert(bytes[0] == 'S' && bytes[1] == 'T');
    assert(cdc->write(session, (const uint8_t *)"OK", 2, 30) == 2);
    assert(cdc->close(session));
    assert(releases == 2 && controls == 2 && reads == 1 && writes == 1);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    void *lib = dlopen(argv[1], RTLD_NOW);
    assert(lib);
    risc_driver_get_v2_fn get = (risc_driver_get_v2_fn)dlsym(lib, "t5_driver_get");
    assert(get);
    const risc_driver_v2 *driver = get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(driver && strcmp(driver->capability_id, "serial.port") == 0);
    const risc_usb_cdc_api_v1 *cdc = (const risc_usb_cdc_api_v1 *)driver->capability;

    risc_usb_host_api_v1 host = {
        RISC_USB_HOST_API_V1, sizeof(host), NULL,
        configuration, claim, release_claim, control, bulk_read, bulk_write
    };
    risc_provider_dependency_v1 dep = {"usb.host", 1, &host};
    assert(driver->start(&dep, 1));

    exercise(cdc); /* ST-LINK/V2.1 VCP */
    claims = releases = reads = writes = controls = 0;
    pid = 0x374e;
    exercise(cdc); /* ST-LINK/V3E VCP */

    driver->stop();
    assert(dlclose(lib) == 0);
    puts("ST-LINK V2.1/V3 CDC VCP is reachable through serial.port: PASS");
    return 0;
}
