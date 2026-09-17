/* TEST FIXTURE ONLY: emulates BQ25896 registers behind a real generic
 * provider ELF so the physical power driver can run without energizing VBUS.
 * This is not a production device driver or a firmware fallback. */
#include "RiscI2cBusV1.h"
#include <stdint.h>

static uint8_t regs[0x20];
static uint64_t claim, sequence;
static bool running;

static bool claim_device(void *ctx, uint8_t address, uint64_t *out) {
    (void)ctx;
    if (out) *out = 0;
    if (!running || claim || !out || address != 0x6bu) return false;
    claim = ++sequence;
    *out = claim;
    return true;
}
static bool transact(void *ctx, uint64_t token, const uint8_t *wr, size_t nw,
                     uint8_t *rd, size_t nr, uint32_t timeout_ms) {
    (void)ctx;
    if (!running || !claim || token != claim || !wr || !nw ||
        wr[0] >= sizeof(regs) || timeout_ms != 100u) return false;
    if (nw == 1 && nr == 1 && rd) {
        const uint8_t reg = wr[0];
        if (reg == 0x0bu) *rd = (regs[3] & 0x20u) ? 0xe0u : 0u;
        else if (reg == 0x11u)
            *rd = ((regs[3] & 0x20u) && (regs[2] & 0x40u)) ? 25u : 0u;
        else *rd = regs[reg];
        return true;
    }
    if (nw == 2 && nr == 0 && !rd) {
        regs[wr[0]] = wr[1];
        return true;
    }
    return false;
}
static bool release_device(void *ctx, uint64_t token) {
    (void)ctx;
    if (!running || !claim || token != claim) return false;
    claim = 0;
    return true;
}
static bool start(const risc_provider_dependency_v1 *deps, size_t n) {
    (void)deps;
    if (running || n) return false;
    for (size_t i = 0; i < sizeof(regs); ++i) regs[i] = 0;
    regs[2] = 0x15u; regs[3] = 0x10u; regs[0x0a] = 0x32u;
    running = true;
    return true;
}
static bool quiesce(void) { return !claim; }
static void stop(void) { if (!claim) running = false; }
static const risc_i2c_bus_api_v1 api = {
    RISC_I2C_BUS_API_V1, sizeof(risc_i2c_bus_api_v1), NULL,
    claim_device, transact, release_device
};
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "fixture-i2c", "i2c.bus", RISC_I2C_BUS_API_V1,
    &api, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : NULL;
}
