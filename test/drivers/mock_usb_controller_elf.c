#include "RiscUsbInterruptV1.h"

/* TEST FIXTURE ONLY. It does not operate ESP32 silicon and MUST NOT be
 * catalog-published. Physical host/controller remain separate ELF providers. */
static const uint8_t configuration_bytes[] = {
    9,2,41,0,2,1,0,0x80,50,
    9,4,0,0,0,2,2,1,0,
    9,4,1,0,2,10,0,0,0,
    7,5,0x81,2,64,0,0,
    7,5,0x02,2,64,0,0
};
static bool running, attached;
static unsigned claims, seq;
static int32_t next_event(void *ctx, risc_usb_controller_event_v1 *event) {
    (void)ctx;
    if (!running || !event) return -1;
    if (attached) return 0;
    attached = true;
    *event = (risc_usb_controller_event_v1){1, 77};
    return 1;
}
static bool configuration(void *ctx, uint64_t device, uint8_t *dst,
                          size_t *length, uint16_t *vid, uint16_t *pid) {
    (void)ctx;
    if (!running || device != 77 || !dst || !length ||
        !vid || !pid || *length < sizeof(configuration_bytes)) return false;
    for (size_t i = 0; i < sizeof(configuration_bytes); ++i)
        dst[i] = configuration_bytes[i];
    *length = sizeof(configuration_bytes);
    *vid = 0x1234;
    *pid = 0x5678;
    return true;
}
static bool claim(void *ctx, uint64_t device, uint8_t iface,
                  uint8_t alt, uint64_t *token) {
    (void)ctx;
    if (!running || device != 77 || iface > 1 || alt || !token) return false;
    *token = ((uint64_t)++seq << 8) | iface;
    ++claims;
    return true;
}
static bool release_claim(void *ctx, uint64_t token) {
    (void)ctx;
    if (!running || !token || !claims) return false;
    --claims;
    return true;
}
static int32_t control(void *ctx, uint64_t device, uint8_t type,
                       uint8_t request, uint16_t value, uint16_t iface,
                       uint8_t *payload, uint16_t length, uint32_t timeout) {
    (void)ctx; (void)value;
    if (!running || device != 77 || type != 0x21 || iface != 0 ||
        timeout != 1000) return -1;
    if (request == 0x20 && payload && length == 7) return 7;
    if (request == 0x22 && !payload && !length) return 0;
    return -1;
}
static int32_t read_bulk(void *ctx, uint64_t token, uint8_t endpoint,
                         uint8_t *dst, size_t capacity, uint32_t timeout) {
    (void)ctx; (void)timeout;
    if (!running || !token || !claims || endpoint != 0x81 ||
        !dst || capacity < 2) return -1;
    dst[0] = 'O'; dst[1] = 'K';
    return 2;
}
static int32_t write_bulk(void *ctx, uint64_t token, uint8_t endpoint,
                          const uint8_t *src, size_t length, uint32_t timeout) {
    (void)ctx; (void)timeout;
    if (!running || !token || !claims || endpoint != 0x02 ||
        !src || !length || length > RISC_USB_CONFIG_LIMIT) return -1;
    return (int32_t)length;
}
static int32_t read_interrupt(void *ctx, uint64_t token, uint8_t endpoint,
                              uint8_t *dst, size_t capacity, uint32_t timeout) {
    (void)ctx; (void)token; (void)endpoint; (void)dst;
    (void)capacity; (void)timeout;
    return -1; /* This fixture models a bulk-only CDC device. */
}
static bool controller_quiesce(void *ctx) { (void)ctx; return claims == 0; }
static const risc_usb_controller_interrupt_v1 api = {
    {RISC_USB_CONTROLLER_API_V1, sizeof(risc_usb_controller_interrupt_v1), 0,
     next_event, configuration, claim, release_claim, control,
     read_bulk, write_bulk, controller_quiesce},
    read_interrupt
};
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (running || deps || count) return false;
    attached = false;
    running = true;
    return true;
}
static bool quiesce(void) { return controller_quiesce(0); }
static void stop(void) { if (quiesce()) running = false; }
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "fixture-usb-controller", "usb.controller", RISC_USB_CONTROLLER_API_V1,
    &api.controller, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t version) {
    return version == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : 0;
}
