/* X4 Pro CW2017 gauge. Reads VCELL and SOC through i2c.bus. No BATINFO write. */
#include "RiscBatteryGaugeV1.h"
#include "RiscI2cBusV1.h"
#include "x4pro_mmio.h"
#include "x4pro_pins.h"
#include "x4pro_proto.h"
#include <stddef.h>
#include <stdint.h>

static const risc_i2c_bus_api_v1 *bus;
static uint64_t claim;
static bool started;
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
static bool read_reg(uint8_t reg, uint8_t *out, size_t length) {
    return bus && claim && bus->transact(bus->context, claim, &reg, 1, out, length, 20);
}
static bool read(void *context, risc_battery_sample_v1 *out) {
    (void)context;
    if (!started || !out) return false;
    uint8_t cell[2] = {0}, soc = 0;
    if (!read_reg(0x02, cell, 2) || !read_reg(0x04, &soc, 1)) { fail("cw2017 read"); return false; }
    out->millivolts = x4pro_cw2017_millivolts(cell[0], cell[1]);
    out->percent = soc;
    out->charging = x4pro_pin_read(X4PRO_PIN_CHG_STAT) ? 0 : 1;
    return true;
}
static const risc_battery_gauge_api_v1 api = {
    RISC_BATTERY_GAUGE_API_V1, sizeof(api), 0, read
};
static bool start(const risc_provider_dependency_v1 *dependencies, size_t count) {
    bus = 0; claim = 0;
    for (size_t i = 0; i < count; ++i)
        if (equal(dependencies[i].capability_id, "i2c.bus") && dependencies[i].api_version == 1)
            bus = dependencies[i].api;
    if (!bus || !bus->claim_device(bus->context, X4PRO_I2C_CW2017, &claim)) { fail("cw2017 claim"); return false; }
    uint8_t version = 0;
    if (!read_reg(0x00, &version, 1)) { fail("cw2017 version"); bus->release_device(bus->context, claim); return false; }
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
    { RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_diagnostics_v2), "x4pro-battery",
      "board.battery", 1, &api, start, stop, quiesce },
    last_error
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    if (abi != RISC_PROVIDER_DRIVER_ABI_V2) return 0;
    return &driver.base;
}
