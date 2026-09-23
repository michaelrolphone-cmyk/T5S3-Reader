/* Integrate the actual dynamically loaded host and CDC/CP210x/CH34x ELFs.
 * The only fake is the physical controller: descriptor transactions, claims,
 * control requests and bounded bulk transfers cross the real ELF boundary. */
#include "RiscUsbControllerV1.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static const uint8_t generic_config[] = {
    9, 2, 32, 0, 1, 1, 0, 0x80, 50,
    9, 4, 0, 0, 2, 0xff, 0, 0, 0,
    7, 5, 0x81, 2, 64, 0, 0,
    7, 5, 0x02, 2, 64, 0, 0
};
static const uint8_t cdc_config[] = {
    9, 2, 46, 0, 2, 1, 0, 0x80, 50,
    9, 4, 0, 0, 0, 2, 2, 1, 0,
    5, 0x24, 6, 0, 1,
    9, 4, 1, 0, 2, 10, 0, 0, 0,
    7, 5, 0x81, 2, 64, 0, 0,
    7, 5, 0x02, 2, 64, 0, 0
};
static risc_usb_controller_event_v1 events[16];
static size_t event_head, event_tail;
static uint64_t physical_sequence = 100, claim_sequence = 1000;
static uint16_t active_vid, active_pid;
static int live_claims, claim_calls, release_calls, control_calls;
static int read_calls, write_calls;
static bool fail_release;

static void enqueue(uint32_t kind, uint64_t physical) {
    assert(event_tail < sizeof(events) / sizeof(events[0]));
    events[event_tail++] = (risc_usb_controller_event_v1){kind, physical};
}
static int32_t next_event(void *ctx, risc_usb_controller_event_v1 *event) {
    (void)ctx;
    if (event_head == event_tail) return 0;
    *event = events[event_head++];
    return 1;
}
static bool configuration(void *ctx, uint64_t physical, uint8_t *bytes,
                          size_t *length, uint16_t *vid, uint16_t *pid) {
    (void)ctx;
    if (!physical || !bytes || !length || !vid || !pid) return false;
    const uint8_t *source = active_vid == 0x2341u ? cdc_config : generic_config;
    const size_t size = active_vid == 0x2341u ? sizeof(cdc_config) : sizeof(generic_config);
    if (*length < size) return false;
    memcpy(bytes, source, size);
    *length = size;
    *vid = active_vid;
    *pid = active_pid;
    return true;
}
static bool claim(void *ctx, uint64_t physical, uint8_t iface, uint8_t alt,
                  uint64_t *out) {
    (void)ctx;
    if (!physical || !out || alt || iface > (active_vid == 0x2341u ? 1u : 0u))
        return false;
    *out = ++claim_sequence;
    ++claim_calls;
    ++live_claims;
    return true;
}
static bool release_claim(void *ctx, uint64_t claim_token) {
    (void)ctx;
    assert(claim_token > 1000 && live_claims > 0);
    ++release_calls;
    if (fail_release) return false;
    --live_claims;
    return true;
}
static int32_t control(void *ctx, uint64_t physical, uint8_t request_type,
                       uint8_t request, uint16_t value, uint16_t index,
                       uint8_t *payload, uint16_t length, uint32_t timeout) {
    (void)ctx; (void)value;
    assert(physical && timeout && live_claims > 0);
    ++control_calls;
    if (active_vid == 0x2341u) {
        assert(request_type == 0x21u && index == 0u);
        assert((request == 0x20u && length == 7u && payload) ||
               (request == 0x22u && length == 0u));
    } else if (active_vid == 0x10c4u) {
        assert(request_type == 0x41u && index == 0u);
        assert(request == 0x00u || request == 0x1eu || request == 0x03u ||
               request == 0x07u);
    } else if (active_vid == 0x1a86u) {
        if (request == 0x9au) {
            /* CH34x write-register request carries divisor/LCR data in index. */
            assert((value == 0x1312u && index != 0u) ||
                   (value == 0x2518u && index == 0xc3u));
        } else assert(index == 0u);
        assert((request_type == 0xc0u && request == 0x5fu &&
                length == 2u && payload) ||
               (request_type == 0x40u && (request == 0xa1u ||
                request == 0x9au || request == 0xa4u)));
        if (request == 0x5fu) { payload[0] = 0x30u; payload[1] = 0u; }
    } else {
        assert(active_vid == 0xcafeu && active_pid == 0x4001u && index == 0u);
        assert(request_type == 0x41u);
        assert((request == 0x30u && length == 7u && payload) ||
               (request == 0x31u && length == 0u));
    }
    return length;
}
static int32_t bulk_read(void *ctx, uint64_t claim_token, uint8_t endpoint,
                         uint8_t *bytes, size_t capacity, uint32_t timeout) {
    (void)ctx;
    assert(claim_token > 1000 && endpoint == 0x81 && bytes && capacity && timeout);
    ++read_calls;
    const size_t n = capacity < 3 ? capacity : 3;
    memset(bytes, 0x5a, n);
    return (int32_t)n;
}
static int32_t bulk_write(void *ctx, uint64_t claim_token, uint8_t endpoint,
                          const uint8_t *bytes, size_t length, uint32_t timeout) {
    (void)ctx;
    assert(claim_token > 1000 && endpoint == 0x02 && bytes && length && timeout);
    ++write_calls;
    return length < 2 ? (int32_t)length : 2;
}
static bool physical_quiesce(void *ctx) { (void)ctx; return live_claims == 0; }

static const risc_driver_v2 *load(void *library, const char *id) {
    assert(library);
    risc_driver_get_v2_fn get = (risc_driver_get_v2_fn)dlsym(library, "t5_driver_get");
    assert(get && !get(1));
    const risc_driver_v2 *driver = get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(driver && driver->quiesce && !strcmp(driver->driver_id, id));
    return driver;
}
static uint64_t snapshot_one(const risc_usb_host_snapshot_v1 *snapshot) {
    risc_usb_device_identity_v1 devices[RISC_USB_HOST_MAX_DEVICES] = {{0}};
    size_t count = RISC_USB_HOST_MAX_DEVICES;
    assert(snapshot->snapshot(snapshot->discovery.host.context, devices, &count));
    assert(count == 1 && devices[0].identified && devices[0].token);
    assert(devices[0].vid == active_vid && devices[0].pid == active_pid);
    return devices[0].token;
}
static void scenario(const risc_driver_v2 *driver,
                     const risc_usb_host_snapshot_v1 *snapshot,
                     uint16_t vid, uint16_t pid) {
    active_vid = vid;
    active_pid = pid;
    const risc_usb_cdc_api_v1 *serial = (const risc_usb_cdc_api_v1 *)driver->capability;
    assert(serial && serial->api_version == RISC_USB_CDC_API_V1 &&
           serial->struct_size >= sizeof(*serial));
    assert(serial->open && serial->close && serial->configure &&
           serial->control_lines && serial->read && serial->write);
    risc_provider_dependency_v1 dependency = {"usb.host", 1, &snapshot->discovery.host};
    assert(driver->start(&dependency, 1));
    const uint64_t physical = ++physical_sequence;
    enqueue(1, physical);
    const uint64_t device = snapshot_one(snapshot);
    const uint64_t token = serial->open(device);
    assert(token && live_claims > 0 && !driver->quiesce());
    assert(serial->configure(token, 115200, 8, 0, 1));
    assert(serial->control_lines(token, true, false));
    uint8_t bytes[8] = {0};
    assert(serial->read(token, bytes, sizeof(bytes), 100) == 3 && bytes[0] == 0x5a);
    assert(serial->write(token, bytes, sizeof(bytes), 100) == 2);
    fail_release = true;
    assert(!serial->close(token));
    assert(!driver->quiesce() && live_claims > 0);
    // The closing claim cannot be reused or bypassed during uncertain release.
    assert(!serial->open(device));
    fail_release = false;
    assert(serial->close(token) && live_claims == 0);
    assert(driver->quiesce());
    driver->stop();

    // Reconnect obtains a DIFFERENT host generation even for identical VID/PID.
    enqueue(2, physical);
    risc_usb_device_identity_v1 devices[RISC_USB_HOST_MAX_DEVICES] = {{0}};
    size_t count = RISC_USB_HOST_MAX_DEVICES;
    assert(snapshot->snapshot(snapshot->discovery.host.context, devices, &count));
    assert(count == 0);
    assert(!snapshot->discovery.host.configuration(snapshot->discovery.host.context,
           device, bytes, &count, &active_vid, &active_pid));
    const uint64_t replacement = ++physical_sequence;
    enqueue(1, replacement);
    const uint64_t new_device = snapshot_one(snapshot);
    assert(new_device != device);
    assert(driver->start(&dependency, 1));
    const uint64_t new_token = serial->open(new_device);
    assert(new_token && serial->close(new_token));
    assert(driver->quiesce());
    driver->stop();
    enqueue(2, replacement);
    count = RISC_USB_HOST_MAX_DEVICES;
    assert(snapshot->snapshot(snapshot->discovery.host.context, devices, &count) &&
           count == 0 && live_claims == 0);
}

int main(int argc, char **argv) {
    assert(argc == 6);
    void *host_library = dlopen(argv[1], RTLD_NOW);
    void *cdc_library = dlopen(argv[2], RTLD_NOW);
    void *cp_library = dlopen(argv[3], RTLD_NOW);
    void *ch_library = dlopen(argv[4], RTLD_NOW);
    void *witness_library = dlopen(argv[5], RTLD_NOW);
    const risc_driver_v2 *host_driver = load(host_library, "usb-host-v2");
    const risc_driver_v2 *cdc = load(cdc_library, "usb-cdc-acm-v2");
    const risc_driver_v2 *cp = load(cp_library, "usb-cp210x-v2");
    const risc_driver_v2 *ch = load(ch_library, "usb-ch34x-v2");
    const risc_driver_v2 *witness = load(witness_library, "usb-serial-witness");
    risc_usb_controller_api_v1 controller = {
        RISC_USB_CONTROLLER_API_V1, sizeof(controller), NULL, next_event,
        configuration, claim, release_claim, control, bulk_read, bulk_write,
        physical_quiesce
    };
    risc_provider_dependency_v1 dependency = {"usb.controller", 1, &controller};
    assert(host_driver->start(&dependency, 1));
    const risc_usb_host_snapshot_v1 *snapshot =
        (const risc_usb_host_snapshot_v1 *)host_driver->capability;
    assert(snapshot && snapshot->discovery.host.struct_size >= sizeof(*snapshot));
    scenario(cdc, snapshot, 0x2341u, 0x0043u);
    scenario(cp, snapshot, 0x10c4u, 0xea60u);
    scenario(ch, snapshot, 0x1a86u, 0x7523u);
    scenario(witness, snapshot, 0xcafeu, 0x4001u);
    assert(claim_calls == release_calls - 4 && live_claims == 0);
    assert(control_calls >= 11 && read_calls == 4 && write_calls == 4);
    assert(host_driver->quiesce());
    host_driver->stop();
    assert(dlclose(witness_library) == 0 && dlclose(ch_library) == 0 &&
           dlclose(cp_library) == 0 && dlclose(cdc_library) == 0 &&
           dlclose(host_library) == 0);
    puts("Production host + four independently installable serial class ELFs: PASS");
    return 0;
}
