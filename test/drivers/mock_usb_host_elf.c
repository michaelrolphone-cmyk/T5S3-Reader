#include "RiscUsbControllerV1.h"
#include <string.h>

/* Test fixture only: separately dlopen'ed usb.host, never a hardware driver. */
static const uint8_t descriptors[] = {
    9,2,41,0,2,1,0,0x80,50,
    9,4,0,0,0,2,2,1,0,
    9,4,1,0,2,10,0,0,0,
    7,5,0x81,2,64,0,0,
    7,5,0x02,2,64,0,0
};
static bool running;
static uint64_t next_claim;
static unsigned outstanding;
static bool configuration(void *ctx, uint64_t dev, uint8_t *bytes,
                          size_t *length, uint16_t *vid, uint16_t *pid) {
    (void)ctx;
    if (!running || dev != 42 || !bytes || !length ||
        *length < sizeof(descriptors) || !vid || !pid) return false;
    memcpy(bytes, descriptors, sizeof(descriptors));
    *length = sizeof(descriptors);
    *vid = 0x1234;
    *pid = 0x5678;
    return true;
}
static bool claim(void *ctx, uint64_t dev, uint8_t iface,
                  uint8_t alt, uint64_t *token) {
    (void)ctx;
    if (!running || dev != 42 || iface > 1 || alt || !token) return false;
    *token = (++next_claim << 8) | iface;
    ++outstanding;
    return true;
}
static bool release_checked(void *ctx, uint64_t token) {
    (void)ctx;
    if (!running || !token || !outstanding) return false;
    --outstanding;
    return true;
}
static void release_claim(void *ctx, uint64_t token) {
    (void)release_checked(ctx, token);
}
static int32_t control(void *ctx, uint64_t dev, uint8_t request_type,
                       uint8_t request, uint16_t value, uint16_t index,
                       uint8_t *payload, uint16_t length, uint32_t timeout_ms) {
    (void)ctx; (void)value;
    if (!running || dev != 42 || request_type != 0x21 || index != 0 ||
        timeout_ms != 1000) return -1;
    if (request == 0x20 && payload && length == 7) return 7;
    if (request == 0x22 && !payload && length == 0) return 0;
    return -1;
}
static int32_t control_claim(void *ctx, uint64_t token, uint8_t request_type,
                             uint8_t request, uint16_t value, uint16_t index,
                             uint8_t *payload, uint16_t length, uint32_t timeout_ms) {
    if (!token || !outstanding || (token & 0xffu) != index) return -1;
    return control(ctx, 42, request_type, request, value, index,
                   payload, length, timeout_ms);
}
static int32_t read_bulk(void *ctx, uint64_t token, uint8_t ep, uint8_t *dst,
                         size_t capacity, uint32_t timeout) {
    (void)ctx; (void)timeout;
    if (!running || !token || ep != 0x81 || !dst || capacity < 2) return -1;
    dst[0] = 'O'; dst[1] = 'K';
    return 2;
}
static int32_t write_bulk(void *ctx, uint64_t token, uint8_t ep,
                          const uint8_t *src, size_t length, uint32_t timeout) {
    (void)ctx; (void)timeout;
    if (!running || !token || ep != 0x02 || !src || !length ||
        length > RISC_USB_CONFIG_LIMIT) return -1;
    return (int32_t)length;
}
static bool poll(void *ctx, size_t maximum, size_t *processed) {
    (void)ctx;
    if (!running || !maximum || !processed) return false;
    *processed = 0;
    return true;
}
static bool devices(void *ctx, uint64_t *out, size_t *count) {
    (void)ctx;
    if (!running || !count) return false;
    if (*count < 1 || !out) { *count = 1; return false; }
    out[0] = 42;
    *count = 1;
    return true;
}
static const risc_usb_host_discovery_v1 usb_host = {
    {RISC_USB_HOST_API_V1, sizeof(risc_usb_host_discovery_v1), 0,
     configuration, claim, release_claim, control, read_bulk, write_bulk},
    poll, devices,  0, 0, release_checked, control_claim
};
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (running || deps || count) return false;
    running = true;
    return true;
}
static bool quiesce(void) { return outstanding == 0; }
static void stop(void) { if (quiesce()) running = false; }
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "fixture-usb-host", "usb.host", RISC_USB_HOST_API_V1,
    &usb_host.host, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : 0;
}
