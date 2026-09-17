/* T5S3 BQ25896 VBUS source provider. Charger register I/O lives HERE and
 * uses an independently installed i2c.bus controller provider. No compiled
 * firmware USB, Wire, charger or board-management forwarding is permitted.
 * Calls must be serialized by the generic provider executor. This provider
 * must not activate while legacy firmware owns the charger or OTG role. */
#include "RiscUsbVbusV1.h"
#include "RiscI2cBusV1.h"
#include "RiscPlatformClockV1.h"
#include <stddef.h>
#include <stdint.h>

#define BQ_ADDRESS 0x6bu /* T5S3 hardware profile */
#define REG_POWER 0x03u
#define REG_BOOST 0x0au
#define REG_STATUS 0x0bu
#define REG_FAULT 0x0cu
#define REG_VBUS_ADC 0x11u
#define OTG_ENABLE 0x20u
#define CHARGE_ENABLE 0x10u
#define VBUS_STATUS_MASK 0xe0u
#define VBUS_OTG 0xe0u
#define POWER_GOOD 0x04u
#define BOOST_FAULT 0x40u
#define VBUS_GOOD 0x80u
#define BOOST_500MA 0x00u
#define BOOST_VOLTAGE_5126MV 0x90u
#define BUS_TIMEOUT_MS 100u
#define STARTUP_TIMEOUT_MS 300u
#define SHUTDOWN_TIMEOUT_MS 400u

static const risc_i2c_bus_api_v1 *bus;
static const risc_platform_clock_api_v1 *clock_api;
static uint64_t bus_claim, lease, sequence;
static uint8_t saved_power, saved_boost;
static bool started, saved, source_requested, faulted;

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static bool read_reg(uint8_t reg, uint8_t *out) {
    return bus && bus_claim && out && bus->transact(bus->context, bus_claim,
             &reg, 1u, out, 1u, BUS_TIMEOUT_MS);
}
static bool write_reg(uint8_t reg, uint8_t value) {
    uint8_t command[2] = {reg, value};
    return bus && bus_claim && bus->transact(bus->context, bus_claim,
             command, 2u, NULL, 0u, BUS_TIMEOUT_MS);
}
static bool timed_out(uint64_t begun, uint32_t limit_ms) {
    uint64_t now = clock_api->monotonic_ms(clock_api->context);
    return now < begun || now - begun >= limit_ms;
}
static void delay_ms(uint32_t milliseconds) {
    clock_api->sleep_ms(clock_api->context, milliseconds);
}
/* External power may appear during shutdown: only charger sourcing must be
 * proven OFF. A failed read is unknown, never evidence of safe shutdown. */
static bool wait_source_off(void) {
    uint64_t begun = clock_api->monotonic_ms(clock_api->context);
    for (;;) {
        uint8_t power = 0, status = 0;
        if (!read_reg(REG_POWER, &power) || !read_reg(REG_STATUS, &status))
            return false;
        if (!(power & OTG_ENABLE) && (status & VBUS_STATUS_MASK) != VBUS_OTG)
            return true;
        if (timed_out(begun, SHUTDOWN_TIMEOUT_MS)) return false;
        delay_ms(10u);
    }
}
/* If any operation fails, retain lease AND lower-provider pin. This covers
 * write-ack ambiguity, rail discharge, register restoration and readback. */
static bool disable_and_restore(void) {
    if (!saved || !write_reg(REG_POWER, saved_power & (uint8_t)~OTG_ENABLE))
        return false;
    if (!wait_source_off() || !write_reg(REG_BOOST, saved_boost)) return false;
    uint8_t power = 0, boost = 0;
    if (!read_reg(REG_POWER, &power) || !read_reg(REG_BOOST, &boost) ||
        (power & (OTG_ENABLE | CHARGE_ENABLE)) !=
            (saved_power & CHARGE_ENABLE) || boost != saved_boost) return false;
    source_requested = false;
    return true;
}
static bool preflight(void) {
    uint8_t status = 0, adc = 0, power = 0;
    return read_reg(REG_STATUS, &status) && read_reg(REG_VBUS_ADC, &adc) &&
           read_reg(REG_POWER, &power) &&
           (status & (VBUS_STATUS_MASK | POWER_GOOD)) == 0 &&
           (adc & VBUS_GOOD) == 0 && (power & OTG_ENABLE) == 0;
}
static bool verify_source(void) {
    uint64_t begun = clock_api->monotonic_ms(clock_api->context);
    for (;;) {
        uint8_t power = 0, status = 0, adc = 0, faults = 0;
        if (!read_reg(REG_POWER, &power) || !read_reg(REG_STATUS, &status) ||
            !read_reg(REG_VBUS_ADC, &adc) || !read_reg(REG_FAULT, &faults))
            return false;
        if ((faults & BOOST_FAULT) || !(power & OTG_ENABLE)) return false;
        /* REG11[6:0] reads 2.6V + 100mV/count; 18 indicates >=4.4V. */
        if ((status & VBUS_STATUS_MASK) == VBUS_OTG &&
            (adc & VBUS_GOOD) && (adc & 0x7fu) >= 18u) return true;
        if (timed_out(begun, STARTUP_TIMEOUT_MS)) return false;
        delay_ms(10u);
    }
}
static bool acquire_host(void *unused, uint32_t requested_ma, uint64_t *out) {
    (void)unused;
    if (out) *out = 0;
    if (!out || !started || !bus_claim || lease || faulted ||
        !requested_ma || requested_ma > 500u || sequence == UINT64_MAX ||
        !preflight() || !read_reg(REG_POWER, &saved_power) ||
        !read_reg(REG_BOOST, &saved_boost)) return false;
    saved = true;
    lease = ++sequence; /* A partially applied write MUST pin the provider. */
    const uint8_t boost = (uint8_t)((saved_boost & 0x08u) |
                                 BOOST_VOLTAGE_5126MV | BOOST_500MA);
    bool ok = write_reg(REG_BOOST, boost);
    if (ok) {
        const uint8_t value = (uint8_t)((saved_power &
                                 (uint8_t)~CHARGE_ENABLE) | OTG_ENABLE);
        source_requested = true; /* Even a failed write may have applied. */
        ok = write_reg(REG_POWER, value);
    }
    if (ok) ok = verify_source();
    if (!ok) {
        if (disable_and_restore()) { lease = 0; saved = false; }
        else faulted = true;
        return false;
    }
    *out = lease;
    return true;
}
static bool release_host(void *unused, uint64_t id) {
    (void)unused;
    if (!started || !id || id != lease || !saved) return false;
    /* Retry is allowed even after a prior failure; never issue a new lease. */
    if (!disable_and_restore()) { faulted = true; return false; }
    lease = 0;
    saved = false;
    faulted = false;
    return true;
}
/* Driver quiescence must release the I2C claim HERE, not in stop(): module
 * loaders may unmap immediately after quiesce succeeds and stop returns. */
static bool quiesce(void *unused) {
    (void)unused;
    if (lease || source_requested) return false;
    if (bus_claim) {
        if (!bus || !bus->release_device(bus->context, bus_claim)) {
            faulted = true;
            return false;
        }
        bus_claim = 0;
    }
    faulted = false;
    return true;
}
static bool driver_quiesce(void) { return quiesce(NULL); }
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (started || bus_claim || lease || faulted || !deps || count != 2u) return false;
    const risc_i2c_bus_api_v1 *b = NULL;
    const risc_platform_clock_api_v1 *t = NULL;
    for (size_t i = 0; i < count; ++i) {
        if (equal(deps[i].capability_id, "i2c.bus") &&
            deps[i].api_version == RISC_I2C_BUS_API_V1 && !b)
            b = (const risc_i2c_bus_api_v1 *)deps[i].api;
        else if (equal(deps[i].capability_id, "platform.clock") &&
                 deps[i].api_version == RISC_PLATFORM_CLOCK_API_V1 && !t)
            t = (const risc_platform_clock_api_v1 *)deps[i].api;
        else return false;
    }
    if (!b || b->api_version != RISC_I2C_BUS_API_V1 ||
        b->struct_size < sizeof(*b) || !b->claim_device || !b->transact ||
        !b->release_device || !t || t->api_version != RISC_PLATFORM_CLOCK_API_V1 ||
        t->struct_size < sizeof(*t) || !t->monotonic_ms || !t->sleep_ms) return false;
    bus = b; clock_api = t;
    uint64_t acquired = 0;
    if (!bus->claim_device(bus->context, BQ_ADDRESS, &acquired) || !acquired) {
        bus = NULL; clock_api = NULL; return false;
    }
    bus_claim = acquired;
    started = true;
    uint8_t power = 0;
    if (!read_reg(REG_POWER, &power)) return false; /* loader retries quiesce */
    return true;
}
static void stop(void) {
    /* quiesce must have fully released both the VBUS and I2C leases. */
    if (lease || bus_claim || source_requested || faulted) return;
    bus = NULL; clock_api = NULL; started = false; saved = false;
}
static const risc_usb_vbus_api_v1 capability = {
    RISC_USB_VBUS_API_V1, sizeof(risc_usb_vbus_api_v1), NULL,
    acquire_host, release_host, quiesce
};
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "board-power-t5s3-v2", "board.power.vbus", RISC_USB_VBUS_API_V1,
    &capability, start, stop, driver_quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : NULL;
}
