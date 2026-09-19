/* T5S3 BQ25896 single-chip owner. OTG, charger register I/O and charge
 * configuration use ONE independently installed i2c.bus device claim.
 * Calls must be serialized by the generic provider executor. This provider
 * must not activate while legacy firmware owns the charger or OTG role. */
#include "RiscUsbVbusV1.h"
#include "RiscI2cBusV1.h"
#include "RiscPlatformClockV1.h"
#include "BqChargerProfile.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define BQ_ADDRESS 0x6bu /* T5S3 hardware profile */
#define REG_ADC_CONTROL 0x02u
#define REG_POWER 0x03u
#define REG_BOOST 0x0au
#define REG_STATUS 0x0bu
#define REG_FAULT 0x0cu
#define REG_BAT_ADC 0x0eu
#define REG_VBUS_ADC 0x11u
#define ADC_CONTINUOUS 0x40u
#define OTG_ENABLE 0x20u
#define CHARGE_ENABLE 0x10u
#define VBUS_STATUS_MASK 0xe0u
#define VBUS_OTG 0xe0u
#define POWER_GOOD 0x04u
#define BOOST_FAULT 0x40u
#define VBUS_GOOD 0x80u
/* RiscRTE v1.2.16 set REG0A BOOST_LIM=010 (1.2 A) and BOOSTV=1001.
 * Keep the 500mA logical USB request admission separate from the PMIC's
 * board-qualified peak/short-circuit threshold. */
#define BOOST_1200MA 0x02u
#define BOOST_VOLTAGE_5126MV 0x90u
#define BUS_TIMEOUT_MS 100u
#define BOOST_SETTLE_MS 80u /* Matches the working v1.2.16 OTG sequence. */
/* REG02 continuous ADC produces new results at 1s intervals; a 300ms
 * deadline falsely rejected its first measurement. */
#define STARTUP_TIMEOUT_MS 1500u
#define SHUTDOWN_TIMEOUT_MS 400u

static const risc_i2c_bus_api_v1 *bus;
static const risc_platform_clock_api_v1 *clock_api;
static uint64_t bus_claim, lease, sequence;
static uint8_t saved_power, saved_boost, saved_adc;
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
static bool charger_read(void *unused, uint8_t reg, uint8_t *out) {
    (void)unused;
    return read_reg(reg, out);
}
static bool charger_write(void *unused, uint8_t reg, uint8_t value) {
    (void)unused;
    return write_reg(reg, value);
}
/* A failed profile write can be partially applied even on NACK. Retain the
 * bus claim and refuse USB sourcing/unload until an explicit profile retry
 * verifies the ENTIRE board policy; do not restore unknown stale registers. */
static bool configure_charger(void *unused) {
    (void)unused;
    if (!started || !bus_claim || lease || source_requested || !bus) return false;
    const risc_bq_charger_io io = {NULL, charger_read, charger_write};
    bool uncertain = false;
    if (!risc_bq_apply_charge_profile(&io, &uncertain)) {
        if (uncertain) {
            faulted = true;
            printf("BQREF failure=charger-profile-uncertain claim-retained=1\n");
        }
        return false;
    }
    faulted = false; /* All profile fields now have matching readbacks. */
    return true;
}
/* A battery consumer never claims 0x6B independently. ADC conversions may
 * be stale; REG0C is deliberately untouched because it clears fault history.
 * Only publish complete snapshots, including when a read fails mid-way. */
static bool read_charger(void *unused, risc_bq25896_charger_snapshot_v1 *out) {
    (void)unused;
    if (!out || !started || !bus_claim || faulted) return false;
    risc_bq25896_charger_snapshot_v1 snapshot = {0};
    if (!read_reg(0x00u, &snapshot.input_control) ||
        !read_reg(REG_ADC_CONTROL, &snapshot.adc_control) ||
        !read_reg(REG_POWER, &snapshot.power_control) ||
        !read_reg(0x04u, &snapshot.charge_current) ||
        !read_reg(0x05u, &snapshot.precharge_termination) ||
        !read_reg(0x06u, &snapshot.charge_voltage) ||
        !read_reg(0x07u, &snapshot.charge_timer) ||
        !read_reg(REG_STATUS, &snapshot.system_status) ||
        !read_reg(REG_BAT_ADC, &snapshot.battery_adc) ||
        !read_reg(0x0fu, &snapshot.system_adc) ||
        !read_reg(REG_VBUS_ADC, &snapshot.vbus_adc)) return false;
    *out = snapshot;
    return true;
}
/* TI SLUSC76C: first REG0C read is latched history, second is live faults.
 * Must be consecutive separate I2C transactions. */
static bool read_fault_pair(uint8_t *latched, uint8_t *live) {
    return latched && live && read_reg(REG_FAULT, latched) &&
           read_reg(REG_FAULT, live);
}
static bool timed_out(uint64_t begun, uint32_t limit_ms) {
    uint64_t now = clock_api->monotonic_ms(clock_api->context);
    return begun == UINT64_MAX || now == UINT64_MAX ||
           now < begun || now - begun >= limit_ms;
}
static void delay_ms(uint32_t milliseconds) {
    clock_api->sleep_ms(clock_api->context, milliseconds);
}
/* External input may appear during shutdown; only sourcing must be OFF.
 * Read failure is unknown, never evidence of safe shutdown. */
static bool wait_source_off(void) {
    uint64_t begun = clock_api->monotonic_ms(clock_api->context);
    if (begun == UINT64_MAX) return false;
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
/* Retain the lease on uncertain write or failed source-off/readback. */
static bool disable_and_restore(void) {
    if (!saved || !write_reg(REG_POWER, saved_power & (uint8_t)~OTG_ENABLE))
        return false;
    if (!wait_source_off() || !write_reg(REG_BOOST, saved_boost) ||
        !write_reg(REG_ADC_CONTROL, saved_adc)) return false;
    uint8_t power = 0, boost = 0, adc = 0;
    if (!read_reg(REG_POWER, &power) || !read_reg(REG_BOOST, &boost) ||
        !read_reg(REG_ADC_CONTROL, &adc) ||
        (power & (OTG_ENABLE | CHARGE_ENABLE)) !=
            (saved_power & CHARGE_ENABLE) || boost != saved_boost ||
        (adc & ADC_CONTINUOUS) != (saved_adc & ADC_CONTINUOUS)) return false;
    source_requested = false;
    return true;
}
static bool preflight(void) {
    uint8_t status = 0, adc = 0, power = 0;
    if (!read_reg(REG_STATUS, &status)) {
        printf("VBUSREF failure=preflight-read reg=0x0b\n");
        return false;
    }
    if (!read_reg(REG_VBUS_ADC, &adc)) {
        printf("VBUSREF failure=preflight-read reg=0x11\n");
        return false;
    }
    if (!read_reg(REG_POWER, &power)) {
        printf("VBUSREF failure=preflight-read reg=0x03\n");
        return false;
    }
    if ((status & (VBUS_STATUS_MASK | POWER_GOOD)) != 0 ||
        (adc & VBUS_GOOD) != 0 || (power & OTG_ENABLE) != 0) {
        printf("VBUSREF failure=preflight-conflict status=0x%02x adc=0x%02x power=0x%02x\n",
               (unsigned)status, (unsigned)adc, (unsigned)power);
        return false;
    }
    uint8_t latched = 0, live = 0;
    if (!read_fault_pair(&latched, &live)) {
        printf("VBUSREF failure=preflight-read reg=0x0c\n");
        return false;
    }
    if (live & BOOST_FAULT) {
        printf("VBUSREF failure=preflight-live-fault prev=0x%02x now=0x%02x\n",
               (unsigned)latched, (unsigned)live);
        return false;
    }
    if (latched & BOOST_FAULT)
        printf("VBUSREF stage=preflight-historical-fault prev=0x%02x now=0x%02x\n",
               (unsigned)latched, (unsigned)live);
    return true;
}
/* Raw register diagnostics: ADC=0 is NOT proof of zero volts because REG11
 * defaults to 2.6V until the first conversion. */
static void report_boost_failure(const char *reason, uint8_t power,
                                 uint8_t status, uint8_t adc,
                                 uint8_t latched, uint8_t live,
                                 uint64_t begun) {
    uint8_t battery = 0xff, boost = 0xff, adc_control = 0xff;
    (void)read_reg(REG_BAT_ADC, &battery);
    (void)read_reg(REG_BOOST, &boost);
    (void)read_reg(REG_ADC_CONTROL, &adc_control);
    uint64_t now = clock_api->monotonic_ms(clock_api->context);
    unsigned elapsed = (now == UINT64_MAX || now < begun) ? 0u :
                       (unsigned)(now - begun);
    printf("VBUSREF failure=%s p=%02x s=%02x v=%02x prev=%02x now=%02x bat=%02x cfg=%02x conv=%02x ms=%u\n",
           reason, (unsigned)power, (unsigned)status, (unsigned)adc,
           (unsigned)latched, (unsigned)live, (unsigned)battery,
           (unsigned)boost, (unsigned)adc_control, elapsed);
}
static bool verify_source(uint64_t begun) {
    if (begun == UINT64_MAX) {
        printf("VBUSREF failure=boost-clock\n");
        return false;
    }
    for (;;) {
        uint8_t power = 0, status = 0, adc = 0, latched = 0, live = 0;
        if (!read_reg(REG_POWER, &power) || !read_reg(REG_STATUS, &status) ||
            !read_reg(REG_VBUS_ADC, &adc) ||
            !read_fault_pair(&latched, &live)) {
            printf("VBUSREF failure=boost-read\n");
            return false;
        }
        if (live & BOOST_FAULT) {
            report_boost_failure("boost-fault", power, status, adc,
                                 latched, live, begun);
            return false;
        }
        if (latched & BOOST_FAULT) {
            report_boost_failure("boost-transient", power, status, adc,
                                 latched, live, begun);
            return false;
        }
        if (!(power & OTG_ENABLE)) {
            report_boost_failure("boost-disabled", power, status, adc,
                                 latched, live, begun);
            return false;
        }
        /* REG11[6:0] = 2.6V + 100mV/count. VBUS_GD describes INPUT and
         * can be zero while OTG is successfully sourcing. */
        if ((status & VBUS_STATUS_MASK) == VBUS_OTG &&
            (adc & 0x7fu) >= 18u) return true; /* at least 4.4 V */
        if (timed_out(begun, STARTUP_TIMEOUT_MS)) {
            report_boost_failure("boost-timeout", power, status, adc,
                                 latched, live, begun);
            return false;
        }
        delay_ms(10u);
    }
}
static bool acquire_host(void *unused, uint32_t requested_ma, uint64_t *out) {
    (void)unused;
    if (out) *out = 0;
    if (!out || !started || !bus_claim || lease || faulted ||
        !requested_ma || requested_ma > 500u || sequence == UINT64_MAX ||
        !clock_api) {
        printf("VBUSREF failure=acquire-state started=%u claimed=%u leased=%u faulted=%u requested_ma=%u\n",
               (unsigned)started, (unsigned)(bus_claim != 0),
               (unsigned)(lease != 0), (unsigned)faulted, (unsigned)requested_ma);
        return false;
    }
    if (clock_api->monotonic_ms(clock_api->context) == UINT64_MAX) {
        printf("VBUSREF failure=acquire-clock\n");
        return false;
    }
    if (!preflight()) return false;
    if (!read_reg(REG_POWER, &saved_power) ||
        !read_reg(REG_BOOST, &saved_boost) ||
        !read_reg(REG_ADC_CONTROL, &saved_adc)) {
        printf("VBUSREF failure=snapshot-read\n");
        return false;
    }
    saved = true;
    lease = ++sequence; /* A partially applied write MUST pin the provider. */
    const uint8_t boost = (uint8_t)((saved_boost & 0x08u) |
                                 BOOST_VOLTAGE_5126MV | BOOST_1200MA);
    bool ok = write_reg(REG_BOOST, boost);
    if (!ok) printf("VBUSREF failure=boost-config-write\n");
    if (ok) {
        ok = write_reg(REG_ADC_CONTROL, saved_adc | ADC_CONTINUOUS);
        if (!ok) printf("VBUSREF failure=adc-config-write\n");
    }
    if (ok) {
        const uint8_t value = (uint8_t)((saved_power &
                                 (uint8_t)~CHARGE_ENABLE) | OTG_ENABLE);
        source_requested = true; /* Even a failed write may have applied. */
        ok = write_reg(REG_POWER, value);
        if (!ok) printf("VBUSREF failure=otg-enable-write\n");
    }
    if (ok) {
        const uint64_t enabled_at = clock_api->monotonic_ms(clock_api->context);
        if (enabled_at == UINT64_MAX) {
            printf("VBUSREF failure=boost-clock\n");
            ok = false;
        } else {
            delay_ms(BOOST_SETTLE_MS);
            ok = verify_source(enabled_at);
        }
    }
    if (!ok) {
        if (disable_and_restore()) {
            lease = 0;
            saved = false;
            printf("VBUSREF stage=rollback-complete\n");
        } else {
            faulted = true;
            printf("VBUSREF failure=rollback-unsafe lease-retained=1\n");
        }
        return false;
    }
    *out = lease;
    printf("VBUSREF stage=source-verified cfg=0x%02x limit-ma=1200 requested-ma=%u\n",
           (unsigned)boost, (unsigned)requested_ma);
    return true;
}
static bool release_host(void *unused, uint64_t id) {
    (void)unused;
    if (!started || !id || id != lease || !saved) return false;
    if (!disable_and_restore()) { faulted = true; return false; }
    lease = 0;
    saved = false;
    faulted = false;
    return true;
}
/* A failed charge-profile transaction or uncertain VBUS restoration MUST
 * retain the chip claim; a software grant release is not hardware quiescence. */
static bool quiesce(void *unused) {
    (void)unused;
    if (lease || source_requested || faulted) return false;
    if (bus_claim) {
        if (!bus || !bus->release_device(bus->context, bus_claim)) {
            faulted = true;
            return false;
        }
        bus_claim = 0;
    }
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
    if (lease || bus_claim || source_requested || faulted) return;
    bus = NULL; clock_api = NULL; started = false; saved = false;
}
static const risc_usb_vbus_charger_api_v1 capability = {
    {RISC_USB_VBUS_API_V1, sizeof(risc_usb_vbus_charger_api_v1), NULL,
     acquire_host, release_host, quiesce}, read_charger, configure_charger
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
