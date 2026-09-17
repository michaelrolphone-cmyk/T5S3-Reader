/* T5S3 BQ25896 VBUS source driver. Real charger register I/O is implemented
 * here through a separately installed exclusive i2c.bus provider. No calls to
 * BoardT5S3, Wire, NativeUsbBridge, or firmware charger procedures.
 *
 * IMPORTANT: activation also requires the old firmware to relinquish USB
 * and charger/I2C ownership. That runtime integration is not implemented.
 * Calls MUST be serialized by the generic provider executor. */
#include "RiscUsbVbusV1.h"
#include "RiscI2cBusV1.h"
#include "RiscPlatformClockV1.h"
#include <stddef.h>
#include <stdint.h>

#define BQ25896_ADDRESS 0x6bu  /* T5S3 pin.hpp, board-specific */
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
#define TRANSACTION_TIMEOUT_MS 100u
#define STARTUP_TIMEOUT_MS 300u
#define SHUTDOWN_TIMEOUT_MS 400u

static const risc_i2c_bus_api_v1 *bus;
static const risc_platform_clock_api_v1 *clock_api;
static uint64_t bus_claim, lease, sequence;
static uint8_t original_power, original_boost;
static bool started, saved, source_requested, faulted;

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static bool read_reg(uint8_t reg, uint8_t *out) {
    return bus && bus_claim && out &&
        bus->transact(bus->context, bus_claim, &reg, 1, out, 1,
                      TRANSACTION_TIMEOUT_MS);
}
static bool write_reg(uint8_t reg, uint8_t value) {
    uint8_t command[2] = {reg, value};
    return bus && bus_claim && bus->transact(bus->context, bus_claim,
        command, sizeof(command), NULL, 0, TRANSACTION_TIMEOUT_MS);
}
static bool elapsed(uint64_t begun, uint32_t timeout) {
    uint64_t now = clock_api->monotonic_ms(clock_api->context);
    /* Monotonic wrap/clock regression is unsafe for voltage stabilization. */
    return now < begun || now - begun >= timeout;
}
static void delay_ms(uint32_t ms) {
    clock_api->sleep_ms(clock_api->context, ms);
}
/* A return of true proves the charger is no longer sourcing. External VBUS
 * may become present during release; the critical condition is OTG disabled. */
static bool wait_source_off(void) {
    uint64_t begun = clock_api->monotonic_ms(clock_api->context);
    for (;;) {
        uint8_t power = 0, status = 0;
        if (!read_reg(REG_POWER, &power) || !read_reg(REG_STATUS, &status))
            return false;
        if (!(power & OTG_ENABLE) && (status & VBUS_STATUS_MASK) != VBUS_OTG)
            return true;
        if (elapsed(begun, SHUTDOWN_TIMEOUT_MS)) return false;
        delay_ms(10);
    }
}
/* Do not relinquish the lease if the bus reports a failed write, the charger
 * still reports OTG, or restoring the board's saved register state fails. */
static bool disable_and_restore(void) {
    if (!saved || !write_reg(REG_POWER, original_power & (uint8_t)~OTG_ENABLE))
        return false;
    if (!wait_source_off()) return false;
    if (!write_reg(REG_BOOST, original_boost)) return false;
    uint8_t boost = 0, power = 0;
    if (!read_reg(REG_BOOST, &boost) || boost != original_boost ||
        !read_reg(REG_POWER, &power) || (power & (OTG_ENABLE | CHARGE_ENABLE)) !=
            (original_power & CHARGE_ENABLE)) return false;
    source_requested = false;
    return true;
}
static bool preflight(void) {
    uint8_t status = 0, adc = 0, power = 0;
    /* Fail CLOSED on missing readings. Source must be absent before boost. */
    return read_reg(REG_STATUS, &status) && read_reg(REG_VBUS_ADC, &adc) &&
           read_reg(REG_POWER, &power) &&
           (status & (VBUS_STATUS_MASK | POWER_GOOD)) == 0 &&
           (adc & VBUS_GOOD) == 0 && (power & OTG_ENABLE) == 0;
}
static bool confirm_source(void) {
    uint64_t begun = clock_api->monotonic_ms(clock_api->context);
    for (;;) {
        uint8_t power = 0, status = 0, adc = 0, faults = 0;
        if (!read_reg(REG_POWER, &power) || !read_reg(REG_STATUS, &status) ||
            !read_reg(REG_VBUS_ADC, &adc) || !read_reg(REG_FAULT, &faults))
            return false;
        if ((faults & BOOST_FAULT) || !(power & OTG_ENABLE)) return false;
        /* BQ25896 ADC: 2.6 V + 100 mV * REG11[6:0]. 18 = 4.4 V. */
        if ((status & VBUS_STATUS_MASK) == VBUS_OTG &&
            (adc & VBUS_GOOD) && (adc & 0x7fu) >= 18u) return true;
        if (elapsed(begun, STARTUP_TIMEOUT_MS)) return false;
        delay_ms(10);
    }
}
static bool acquire_host(void *ctx, uint32_t requested_ma, uint64_t *out) {
    (void)ctx;
    if (out) *out = 0;
    if (!out || !started || !bus_claim || lease || faulted ||
        !requested_ma || requested_ma > 500u || sequence == UINT64_MAX ||
        !preflight()) return false;
    if (!read_reg(REG_POWER, &original_power) ||
        !read_reg(REG_BOOST, &original_boost)) return false;
    saved = true;
    lease = ++sequence;  /* Retain ownership even if a partially written rail fails. */
    const uint8_t boost = (uint8_t)((original_boost & 0x08u) |
                                   BOOST_VOLTAGE_5126MV | BOOST_500MA);
    bool ok = write_reg(REG_BOOST, boost);
    if (ok) {
        const uint8_t configured = (uint8_t)((original_power &
                 (uint8_t)~CHARGE_ENABLE) | OTG_ENABLE);
        ok = write_reg(REG_POWER, configured);
        /* Treat a failed write as potentially applied. */
        source_requested = true;
    }
    if (ok) ok = confirm_source();
    if (!ok) {
        if (disable_and_restore()) { lease = 0; saved = false; }
        else faulted = true;
        return false;
    }
    *out = lease;
    return true;
}
static bool release_host(void *ctx, uint64_t id) {
    (void)ctx;
    if (!started || !id || id != lease || !saved) return false;
    if (!disable_and_restore()) { faulted = true; return false; }
    lease = 0;
    saved = false;
    faulted = false;
    return true;
}
static bool quiesce(void *ctx) {
    (void)ctx;
    return !lease && !source_requested && !faulted;
}
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (started || bus_claim || lease || faulted || !deps || count != 2) return false;
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
    bus = b;
    clock_api = t;
    uint64_t acquired = 0;
    if (!bus->claim_device(bus->context, BQ25896_ADDRESS, &acquired) || !acquired) {
        bus = NULL; clock_api = NULL;
        return false;
    }
    bus_claim = acquired;
    started = true;
    /* Probe without altering any charger configuration. */
    uint8_t reg = 0;
    if (!read_reg(REG_POWER, &reg)) {
        if (bus->release_device(bus->context, bus_claim)) {
            bus_claim = 0; started = false; bus = NULL; clock_api = NULL;
        } else faulted = true;
        return false;
    }
    return true;
}
static void stop(void) {
    if (!quiesce(NULL)) return;
    if (bus_claim && (!bus || !bus->release_device(bus->context, bus_claim))) {
        faulted = true; return;
    }
    bus_claim = 0;
    bus = NULL;
    clock_api = NULL;
    started = false;
}
static const risc_usb_vbus_api_v1 capability = {
    RISC_USB_VBUS_API_V1, sizeof(risc_usb_vbus_api_v1), NULL,
    acquire_host, release_host, quiesce
};
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "board-power-t5s3-v2", "board.power.vbus", RISC_USB_VBUS_API_V1,
    &capability, start, stop, /* Quiesce doesn't release the bus; stop does. */
    (bool (*)(void))0 /* replaced below with a typed adapter */
};
static bool driver_quiesce(void) { return quiesce(NULL); }
/* Keep the lifecycle table immutable and export just the generic root. */
static const risc_driver_v2 lifecycle = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "board-power-t5s3-v2", "board.power.vbus", RISC_USB_VBUS_API_V1,
    &capability, start, stop, driver_quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &lifecycle : NULL;
}
