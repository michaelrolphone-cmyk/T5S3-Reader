/* X4 Pro GT911. Binds i2c.bus. X is at byte 0; portrait controller on landscape panel. */
#include "RiscI2cBusV1.h"
#include "RiscPlatformClockV1.h"
#include "RiscTouchV1.h"
#include "x4pro_pins.h"
#include "x4pro_proto.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const risc_i2c_bus_api_v1 *bus;
static const risc_platform_clock_api_v1 *clock_api;
static uint64_t claim;
static bool started;
static risc_touch_snapshot_v1 snapshot;
static char last_error_text[64];

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void fail(const char *text) {
    size_t i = 0;
    while (text[i] && i + 1u < sizeof(last_error_text)) { last_error_text[i] = text[i]; ++i; }
    last_error_text[i] = 0;
}
static bool read_reg(uint16_t reg, uint8_t *out, size_t length) {
    uint8_t address[2] = {(uint8_t)(reg >> 8), (uint8_t)reg};
    return bus && claim && bus->transact(bus->context, claim, address, 2, out, length, 20);
}
static bool write_reg8(uint16_t reg, uint8_t value) {
    uint8_t command[3] = {(uint8_t)(reg >> 8), (uint8_t)reg, value};
    return bus && claim && bus->transact(bus->context, claim, command, 3, 0, 0, 20);
}
static uint64_t subscribe(void *context) { (void)context; return started ? 1 : 0; }
static bool unsubscribe(void *context, uint64_t subscription) { (void)context; return subscription == 1; }
static bool poll(void *context, size_t max_reports) {
    (void)context;
    if (!started || !max_reports) return false;
    uint8_t status = 0;
    if (!read_reg(0x814e, &status, 1)) { fail("gt911 status"); return false; }
    snapshot.contact_count = 0;
    if (status & 0x80u) {
        uint8_t count = status & 0x0fu;
        if (count > RISC_TOUCH_MAX_CONTACTS) count = RISC_TOUCH_MAX_CONTACTS;
        for (uint8_t i = 0; i < count; ++i) {
            uint8_t raw[8] = {0};
            if (!read_reg((uint16_t)(0x814f + i * 8u), raw, 8)) break;
            if (x4pro_gt911_map(raw, &snapshot.contacts[snapshot.contact_count].x,
                                &snapshot.contacts[snapshot.contact_count].y,
                                &snapshot.contacts[snapshot.contact_count].id))
                ++snapshot.contact_count;
        }
        (void)write_reg8(0x814e, 0);
    }
    snapshot.sequence++;
    snapshot.timestamp_ms = clock_api && clock_api->monotonic_ms ? clock_api->monotonic_ms(clock_api->context) : 0;
    snapshot.width = X4PRO_PANEL_WIDTH;
    snapshot.height = X4PRO_PANEL_HEIGHT;
    return true;
}
static int32_t next(void *context, uint64_t subscription, risc_touch_event_v1 *out) {
    (void)context; (void)subscription; (void)out;
    return 0;
}
static bool snapshot_fn(void *context, risc_touch_snapshot_v1 *out) {
    (void)context;
    if (!started || !out) return false;
    *out = snapshot;
    return true;
}
static const risc_touch_api_v1 api = {
    RISC_TOUCH_API_V1, sizeof(api), 0, subscribe, unsubscribe, poll, next, snapshot_fn
};
static bool start(const risc_provider_dependency_v1 *dependencies, size_t count) {
    bus = 0; clock_api = 0; claim = 0;
    for (size_t i = 0; i < count; ++i) {
        if (equal(dependencies[i].capability_id, "i2c.bus") && dependencies[i].api_version == 1) bus = dependencies[i].api;
        if (equal(dependencies[i].capability_id, "platform.clock") && dependencies[i].api_version == 1) clock_api = dependencies[i].api;
    }
    if (!bus || !clock_api || !bus->claim_device(bus->context, X4PRO_I2C_GT911, &claim)) {
        fail("gt911 bus claim");
        return false;
    }
    uint8_t id[4] = {0};
    if (!read_reg(0x8140, id, 4) || id[0] != '9' || id[1] != '1' || id[2] != '1') {
        fail("gt911 id");
        bus->release_device(bus->context, claim);
        return false;
    }
    memset(&snapshot, 0, sizeof(snapshot));
    started = true;
    return true;
}
static void stop(void) {
    if (bus && claim) bus->release_device(bus->context, claim);
    claim = 0; started = false;
}
static bool quiesce(void) { stop(); return true; }
static bool last_error(char *destination, size_t capacity) {
    if (!destination || !capacity || !last_error_text[0]) return false;
    size_t i = 0;
    while (last_error_text[i] && i + 1u < capacity) { destination[i] = last_error_text[i]; ++i; }
    destination[i] = 0;
    return true;
}
static const risc_driver_diagnostics_v2 driver = {
    { RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_diagnostics_v2), "x4pro-gt911",
      "input.touch.raw", 1, &api, start, stop, quiesce },
    last_error
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    if (abi != RISC_PROVIDER_DRIVER_ABI_V2) return 0;
    return &driver.base;
}
