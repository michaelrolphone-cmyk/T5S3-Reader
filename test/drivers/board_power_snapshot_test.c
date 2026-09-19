#include "RiscUsbVbusV1.h"
#include "RiscI2cBusV1.h"
#include "RiscPlatformClockV1.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t registers[0x20];
static unsigned read_count, write_count, fault_reads, releases;
static uint8_t failing_register = 0xffu;
static uint64_t claimed;

static bool claim_device(void *unused, uint8_t address, uint64_t *out) {
    (void)unused;
    if (claimed || !out || address != 0x6bu) return false;
    claimed = 7;
    *out = claimed;
    return true;
}
static bool transact(void *unused, uint64_t token, const uint8_t *write,
                     size_t write_length, uint8_t *read, size_t read_length,
                     uint32_t timeout_ms) {
    (void)unused;
    if (token != claimed || !token || !write || timeout_ms != 100u ||
        !write_length || write[0] >= sizeof(registers)) return false;
    if (write_length == 1u && read_length == 1u && read) {
        ++read_count;
        if (write[0] == 0x0cu) ++fault_reads;
        if (write[0] == failing_register) return false;
        *read = registers[write[0]];
        return true;
    }
    if (write_length == 2u && !read_length && !read) {
        ++write_count;
        registers[write[0]] = write[1];
        return true;
    }
    return false;
}
static bool release_device(void *unused, uint64_t token) {
    (void)unused;
    if (token != claimed || !token) return false;
    claimed = 0;
    ++releases;
    return true;
}
static uint64_t monotonic_ms(void *unused) { (void)unused; return 100u; }
static void sleep_ms(void *unused, uint32_t ms) { (void)unused; (void)ms; }

int main(void) {
    risc_i2c_bus_api_v1 bus = {
        RISC_I2C_BUS_API_V1, sizeof(risc_i2c_bus_api_v1), NULL,
        claim_device, transact, release_device
    };
    risc_platform_clock_api_v1 clock = {
        RISC_PLATFORM_CLOCK_API_V1, sizeof(risc_platform_clock_api_v1), NULL,
        monotonic_ms, sleep_ms
    };
    const risc_provider_dependency_v1 dependencies[] = {
        {"i2c.bus", RISC_I2C_BUS_API_V1, &bus},
        {"platform.clock", RISC_PLATFORM_CLOCK_API_V1, &clock}
    };
    const risc_driver_v2 *driver = t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(driver && strcmp(driver->capability_id, "board.power.vbus") == 0);
    const risc_usb_vbus_api_v1 *base = (const risc_usb_vbus_api_v1 *)driver->capability;
    assert(base && base->api_version == RISC_USB_VBUS_API_V1 &&
           base->struct_size >= sizeof(risc_usb_vbus_charger_api_v1));
    const risc_usb_vbus_charger_api_v1 *owner =
        (const risc_usb_vbus_charger_api_v1 *)driver->capability;
    assert(owner->read_charger && owner->base.acquire_host &&
           owner->base.release_host && owner->base.quiesce);

    risc_bq25896_charger_snapshot_v1 snapshot;
    memset(&snapshot, 0xa5, sizeof(snapshot));
    assert(!owner->read_charger(owner->base.context, &snapshot));
    assert(!owner->read_charger(owner->base.context, NULL));

    registers[0x00] = 0x25u;
    registers[0x02] = 0x40u;
    registers[0x03] = 0x10u;
    registers[0x04] = 0x08u;
    registers[0x05] = 0x11u;
    registers[0x06] = 0x22u;
    registers[0x07] = 0x33u;
    registers[0x0b] = 0x04u;
    registers[0x0e] = 0x17u;
    registers[0x0f] = 0x29u;
    registers[0x11] = 0x80u;
    registers[0x0c] = 0x40u; /* Reading this would clear fault history. */
    assert(driver->start(dependencies, 2u));
    assert(claimed == 7u);
    assert(owner->read_charger(owner->base.context, &snapshot));
    assert(snapshot.input_control == 0x25u);
    assert(snapshot.adc_control == 0x40u);
    assert(snapshot.power_control == 0x10u);
    assert(snapshot.charge_current == 0x08u);
    assert(snapshot.precharge_termination == 0x11u);
    assert(snapshot.charge_voltage == 0x22u);
    assert(snapshot.charge_timer == 0x33u);
    assert(snapshot.system_status == 0x04u);
    assert(snapshot.battery_adc == 0x17u);
    assert(snapshot.system_adc == 0x29u);
    assert(snapshot.vbus_adc == 0x80u);
    assert(fault_reads == 0 && write_count == 0 && read_count == 12u);

    failing_register = 0x06u;
    risc_bq25896_charger_snapshot_v1 sentinel = snapshot;
    assert(!owner->read_charger(owner->base.context, &snapshot));
    assert(memcmp(&sentinel, &snapshot, sizeof(snapshot)) == 0);
    assert(fault_reads == 0 && write_count == 0);
    failing_register = 0xffu;
    assert(driver->quiesce());
    driver->stop();
    assert(!claimed && releases == 1u);
    assert(!owner->read_charger(owner->base.context, &snapshot));
    puts("BQ25896 snapshot: same claimed chip, copied telemetry, no REG0C reads/writes, atomic failure: PASS");
    return 0;
}
