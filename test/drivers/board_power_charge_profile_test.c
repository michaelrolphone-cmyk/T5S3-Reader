#include "RiscUsbVbusV1.h"
#include "RiscBq25896ProfileV1.h"
extern const risc_driver_v2 *t5_profile_get(uint32_t abi);
#include "RiscI2cBusV1.h"
#include "RiscPlatformClockV1.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t reg[0x20];
    uint64_t claim;
    unsigned claims, releases, writes, fault_reads, fail_read_reg;
    int fail_write_reg, fail_write_once;
} fake_bq;
static fake_bq chip;

static bool claim_device(void *ctx, uint8_t addr, uint64_t *out) {
    fake_bq *c = ctx;
    if (addr != 0x6bu || !out || c->claim) return false;
    c->claim = ++c->claims;
    *out = c->claim;
    return true;
}
static bool transact(void *ctx, uint64_t token, const uint8_t *wr, size_t wn,
                     uint8_t *rd, size_t rn, uint32_t timeout) {
    fake_bq *c = ctx;
    if (!token || token != c->claim || !wr || !wn || timeout != 100u ||
        wr[0] >= sizeof(c->reg)) return false;
    if (wn == 1u && rn == 1u && rd) {
        if (wr[0] == c->fail_read_reg) return false;
        if (wr[0] == 0x0cu) ++c->fault_reads;
        *rd = c->reg[wr[0]];
        return true;
    }
    if (wn == 2u && rn == 0u && !rd) {
        ++c->writes;
        c->reg[wr[0]] = wr[1];
        if (c->fail_write_once && c->fail_write_reg == wr[0]) {
            c->fail_write_once = 0;
            return false; /* Register may have accepted data before NACK. */
        }
        return true;
    }
    return false;
}
static bool release_device(void *ctx, uint64_t claim) {
    fake_bq *c = ctx;
    if (!claim || claim != c->claim) return false;
    c->claim = 0;
    ++c->releases;
    return true;
}
static uint64_t monotonic_ms(void *ctx) { (void)ctx; return 0; }
static void sleep_ms(void *ctx, uint32_t duration) { (void)ctx; (void)duration; }
static risc_i2c_bus_api_v1 i2c = {
    RISC_I2C_BUS_API_V1, sizeof(risc_i2c_bus_api_v1), &chip,
    claim_device, transact, release_device
};
static risc_platform_clock_api_v1 clock_api = {
    RISC_PLATFORM_CLOCK_API_V1, sizeof(risc_platform_clock_api_v1), NULL,
    monotonic_ms, sleep_ms
};
static risc_provider_dependency_v1 deps[] = {
    {"i2c.bus", RISC_I2C_BUS_API_V1, &i2c},
    {"platform.clock", RISC_PLATFORM_CLOCK_API_V1, &clock_api},
    {RISC_BQ25896_PROFILE_CAPABILITY, 1, NULL}
};
static const risc_driver_v2 *driver;
static const risc_usb_vbus_charger_api_v1 *charger;

static void begin_chip(void) {
    deps[2].api = t5_profile_get(2)->capability;
    memset(&chip, 0, sizeof(chip));
    chip.fail_read_reg = 0xffu;
    chip.reg[0x00] = 0x80u;
    chip.reg[0x02] = 0x05u;
    chip.reg[0x03] = 0x10u;
    chip.reg[0x04] = 0x80u;
    chip.reg[0x05] = 0x77u;
    chip.reg[0x06] = 0x02u;
    chip.reg[0x07] = 0x3au;
    chip.reg[0x09] = 0x20u;
    chip.reg[0x14] = 0x01u; /* BQ25896 PN=0, revision=1. */
    assert(driver->start(deps, 3u));
    assert(chip.claim && chip.claims == 1u);
}
static void end_chip(void) {
    assert(driver->quiesce());
    assert(chip.releases == 1u && chip.claim == 0u);
    driver->stop();
}
int main(void) {
    driver = t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(driver && driver->capability);
    charger = (const risc_usb_vbus_charger_api_v1 *)driver->capability;
    assert(charger->base.struct_size >= sizeof(*charger));
    assert(charger->read_charger && charger->configure_charger);
    begin_chip();
    assert(charger->configure_charger(NULL));
    assert(chip.claims == 1u && chip.fault_reads == 0u);
    assert(chip.reg[0x00] == 0x52u);
    assert(chip.reg[0x02] == 0x55u);
    assert(chip.reg[0x03] == 0x16u);
    assert(chip.reg[0x04] == 0x88u);
    assert(chip.reg[0x05] == 0u);
    assert(chip.reg[0x06] == 0x5eu);
    assert(chip.reg[0x07] == 0x0au);
    assert(chip.reg[0x09] == 0u);
    unsigned writes = chip.writes;
    assert(charger->configure_charger(NULL) && chip.writes == writes);
    end_chip();

    begin_chip();
    chip.reg[0x03] |= 0x20u; /* Competing OTG source: never disable it. */
    assert(!charger->configure_charger(NULL) && chip.writes == 0u);
    chip.reg[0x03] &= (uint8_t)~0x20u;
    chip.reg[0x0b] = 0xe0u; /* Hardware still reports OTG. */
    assert(!charger->configure_charger(NULL) && chip.writes == 0u);
    chip.reg[0x0b] = 0u;
    end_chip();

    begin_chip();
    chip.fail_read_reg = 0x07u; /* Preflight all registers before changing. */
    assert(!charger->configure_charger(NULL) && chip.writes == 0u);
    chip.fail_read_reg = 0xffu;
    assert(charger->configure_charger(NULL));
    end_chip();

    begin_chip();
    chip.fail_write_reg = 0x04;
    chip.fail_write_once = 1;
    assert(!charger->configure_charger(NULL) && chip.writes != 0u);
    assert(chip.claim && !driver->quiesce());
    uint64_t lease = 77;
    assert(!charger->base.acquire_host(NULL, 500u, &lease) && lease == 0u);
    chip.fail_read_reg = 0x06u;
    assert(!charger->configure_charger(NULL) && !driver->quiesce());
    chip.fail_read_reg = 0xffu;
    assert(charger->configure_charger(NULL)); /* Verified explicit recovery. */
    assert(chip.claims == 1u && chip.fault_reads == 0u);
    end_chip();

    begin_chip();
    chip.reg[0x14] = 0x09u; /* Wrong product number. */
    assert(!charger->configure_charger(NULL) && chip.writes == 0u);
    end_chip();

    puts("BQ25896 charger: same chip claim, board profile, readback, OTG refusal, NACK quarantine and recovery PASS");
    return 0;
}
