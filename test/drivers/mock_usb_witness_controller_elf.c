#include "RiscUsbControllerV1.h"
#include "witness_controller_fixture.h"
static witness_controller_stats stats;
__attribute__((visibility("default")))
witness_controller_stats *fixture_witness_stats(void) { return &stats; }

/* TEST FIXTURE ONLY. It provides one vendor-specific bulk interface matching
 * usb-serial-witness. Production usb.host and the real witness ELF sit above
 * this controller in the provider graph. */
static const uint8_t configuration_bytes[] = {
    9,2,32,0,1,1,0,0x80,50,
    9,4,0,0,2,0xff,0,0,0,
    7,5,0x81,2,64,0,0,
    7,5,0x02,2,64,0,0
};
static bool running, attached;
static unsigned claims, sequence;

static int32_t next_event(void *ctx, risc_usb_controller_event_v1 *event) {
    (void)ctx;
    if (!running || !event) return -1;
    if (stats.detached) {
        if (!attached) return 0;
        attached = false;
        *event = (risc_usb_controller_event_v1){2, 77};
        return 1;
    }
    if (attached) return 0;
    attached = true;
    *event = (risc_usb_controller_event_v1){1, 77};
    return 1;
}
static bool configuration(void *ctx, uint64_t device, uint8_t *dst,
                          size_t *length, uint16_t *vid, uint16_t *pid) {
    (void)ctx;
    if (!running || device != 77 || !dst || !length || !vid || !pid ||
        *length < sizeof(configuration_bytes)) return false;
    for (size_t i = 0; i < sizeof(configuration_bytes); ++i)
        dst[i] = configuration_bytes[i];
    *length = sizeof(configuration_bytes);
    *vid = 0xcafeu;
    *pid = 0x4001u;
    return true;
}
static bool claim(void *ctx, uint64_t device, uint8_t iface,
                  uint8_t alt, uint64_t *token) {
    (void)ctx;
    if (!running || device != 77 || iface != 0 || alt || !token) return false;
    *token = ++sequence;
    ++claims;
    return true;
}
static bool release_claim(void *ctx, uint64_t token) {
    (void)ctx;
    if (!running || !token || !claims || stats.close_error) return false;
    --claims;
    return true;
}
static int32_t control(void *ctx, uint64_t device, uint8_t type,
                       uint8_t request, uint16_t value, uint16_t iface,
                       uint8_t *payload, uint16_t length, uint32_t timeout) {
    (void)ctx; (void)value;
    if (!running || device != 77 || type != 0x41u || iface != 0 ||
        timeout != 1000 || !claims) return -1;
    if (request == 0x30u && payload && length == 7u) return 7;
    if (request == 0x31u && !payload && !length) return 0;
    return -1;
}
static int32_t read_bulk(void *ctx, uint64_t token, uint8_t endpoint,
                         uint8_t *dst, size_t capacity, uint32_t timeout) {
    (void)ctx; (void)timeout;
    if (!running || !token || !claims || endpoint != 0x81u ||
        !dst || capacity < 3) return -1;
    ++stats.reads; stats.read_timeout = timeout;
    dst[0] = 'N'; dst[1] = 'E'; dst[2] = 'W';
    return 3;
}
static int32_t write_bulk(void *ctx, uint64_t token, uint8_t endpoint,
                          const uint8_t *src, size_t length, uint32_t timeout) {
    (void)ctx; (void)timeout;
    if (!running || !token || !claims || endpoint != 0x02u ||
        !src || !length) return -1;
    ++stats.writes; stats.write_timeout = timeout;
    if (stats.write_error) return -1;
    if (stats.blocked) return 0;
    const size_t n = length < 2 ? length : 2;
    if (stats.used + n > sizeof(stats.transmitted)) return -1;
    for (size_t i = 0; i < n; ++i) stats.transmitted[stats.used++] = src[i];
    return (int32_t)n;
}
static bool controller_quiesce(void *ctx) { (void)ctx; return claims == 0; }

static const risc_usb_controller_api_v1 api = {
    RISC_USB_CONTROLLER_API_V1, sizeof(risc_usb_controller_api_v1), 0,
    next_event, configuration, claim, release_claim, control,
    read_bulk, write_bulk, controller_quiesce
};
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (running || deps || count) return false;
    running = true;
    attached = false;
    return true;
}
static bool quiesce(void) { return controller_quiesce(0); }
static void stop(void) { if (quiesce()) running = false; }

static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "fixture-witness-controller", "usb.controller", RISC_USB_CONTROLLER_API_V1,
    &api, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t version) {
    return version == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : 0;
}
