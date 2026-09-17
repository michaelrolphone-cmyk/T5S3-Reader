#include "RiscUsbVbusV1.h"
#include "RiscI2cBusV1.h"
#include "RiscPlatformClockV1.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t regs[0x20];
    uint64_t claim;
    uint64_t next_claim;
    uint64_t now;
    unsigned writes;
    unsigned releases;
    unsigned bad_claims;
    unsigned fail_power_write;
    unsigned fail_release;
    unsigned fail_probe;
    int external;
    int no_boost;
} simulated_board;

static bool claim_device(void *context, uint8_t address, uint64_t *out) {
    simulated_board *b = context;
    if (address != 0x6b || b->claim || !out) return false;
    b->claim = ++b->next_claim;
    *out = b->claim;
    return true;
}
static bool transact(void *context, uint64_t claim,
                     const uint8_t *wr, size_t nwr,
                     uint8_t *rd, size_t nrd, uint32_t timeout) {
    simulated_board *b = context;
    if (claim != b->claim || !claim) { ++b->bad_claims; return false; }
    if (!wr || !nwr || wr[0] >= sizeof(b->regs) || timeout != 100) return false;
    if (nwr == 1 && nrd == 1 && rd) {
        if (b->fail_probe) return false;
        uint8_t reg = wr[0];
        if (reg == 0x0b) {
            if (b->regs[3] & 0x20) *rd = b->no_boost ? 0 : 0xe0;
            else *rd = b->external ? 0x24 : 0;
        } else if (reg == 0x11) {
            if (b->regs[3] & 0x20) *rd = b->no_boost ? 0 : 0x80 | 25;
            else *rd = b->external ? 0x80 : 0;
        } else *rd = b->regs[reg];
        return true;
    }
    if (nwr == 2 && nrd == 0 && !rd) {
        ++b->writes;
        if (wr[0] == 3 && b->fail_power_write) {
            --b->fail_power_write;
            /* An I2C NACK may happen AFTER the PMIC applied the value. */
            b->regs[3] = wr[1];
            return false;
        }
        b->regs[wr[0]] = wr[1];
        return true;
    }
    return false;
}
static bool release_device(void *context, uint64_t claim) {
    simulated_board *b = context;
    if (claim != b->claim || !claim) return false;
    if (b->fail_release) { --b->fail_release; return false; }
    b->claim = 0;
    ++b->releases;
    return true;
}
static uint64_t monotonic_ms(void *context) {
    return ((simulated_board *)context)->now;
}
static void sleep_ms(void *context, uint32_t ms) {
    ((simulated_board *)context)->now += ms;
}
static simulated_board board;
static risc_i2c_bus_api_v1 i2c = {
    RISC_I2C_BUS_API_V1, sizeof(risc_i2c_bus_api_v1),
    &board, claim_device, transact, release_device
};
static risc_platform_clock_api_v1 clock_api = {
    RISC_PLATFORM_CLOCK_API_V1, sizeof(risc_platform_clock_api_v1),
    &board, monotonic_ms, sleep_ms
};
static const risc_provider_dependency_v1 deps[] = {
    {"i2c.bus", RISC_I2C_BUS_API_V1, &i2c},
    {"platform.clock", RISC_PLATFORM_CLOCK_API_V1, &clock_api}
};
static const risc_driver_v2 *driver;
static const risc_usb_vbus_api_v1 *power;
static void reset_board(void) {
    assert(board.claim == 0);
    memset(&board, 0, sizeof(board));
    board.regs[3] = 0x10;
    board.regs[0x0a] = 0x32;
    assert(driver->start(deps, 2));
    assert(board.claim != 0);
}
static void shutdown_board(void) {
    assert(driver->quiesce());
    assert(board.claim == 0);
    driver->stop();
}
int main(void) {
    driver = t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(driver && !t5_driver_get(1));
    assert(strcmp(driver->driver_id, "board-power-t5s3-v2") == 0);
    assert(strcmp(driver->capability_id, "board.power.vbus") == 0);
    power = (const risc_usb_vbus_api_v1 *)driver->capability;
    assert(power && power->api_version == 1 && power->acquire_host &&
           power->release_host && power->quiesce && driver->quiesce);
    assert(!driver->start(deps, 1));

    reset_board();
    uint64_t token = UINT64_MAX;
    assert(!power->acquire_host(NULL, 501, &token) && token == 0);
    assert(!power->acquire_host(NULL, 0, &token) && token == 0);
    assert(board.writes == 0);
    assert(power->acquire_host(NULL, 500, &token) && token != 0);
    assert(board.regs[3] == 0x20);       /* charger off, OTG on */
    assert(board.regs[0x0a] == 0x90);    /* 5.126V and 500mA limit */
    assert(!power->quiesce(NULL) && !driver->quiesce());
    assert(!power->acquire_host(NULL, 500, &(uint64_t){0}));
    assert(!power->release_host(NULL, token + 1));
    board.fail_power_write = 1;
    assert(!power->release_host(NULL, token));
    assert(!driver->quiesce() && board.claim);
    assert(power->release_host(NULL, token)); /* real retry, not soft reset */
    assert(board.regs[3] == 0x10 && board.regs[0x0a] == 0x32);
    assert(!power->release_host(NULL, token));
    board.fail_release = 1;
    assert(!driver->quiesce() && board.claim);
    shutdown_board();
    assert(board.releases == 1 && !board.bad_claims);

    reset_board();
    board.external = 1;
    token = 9;
    assert(!power->acquire_host(NULL, 500, &token) && token == 0);
    assert(board.writes == 0);
    shutdown_board();

    reset_board();
    board.fail_power_write = 1;
    token = 8;
    assert(!power->acquire_host(NULL, 500, &token) && token == 0);
    assert(!board.regs[3] || board.regs[3] == 0x10);
    assert(board.regs[0x0a] == 0x32);
    shutdown_board();

    reset_board();
    board.no_boost = 1;
    token = 8;
    assert(!power->acquire_host(NULL, 500, &token) && token == 0);
    assert(board.now >= 300 && board.regs[3] == 0x10);
    shutdown_board();

    reset_board();
    board.regs[0x0c] = 0x40; /* charger reports boost overcurrent/OVP */
    assert(!power->acquire_host(NULL, 500, &token));
    assert(board.regs[3] == 0x10);
    shutdown_board();
    printf("T5S3 VBUS provider charger sequencing, power conflicts, rollback, timeout and retry: PASS\n");
    return 0;
}
