/* Transitional i2c.bus ELF. Upstream drivers bind only RiscI2cBusV1 and
 * cannot observe whether this provider delegates or owns I2C0 itself.
 * The firmware retains exclusive controller/pin/Wire ownership for now.
 * Only this privileged, verified ELF can import the private compatibility API.
 */
#include "RiscI2cBusV1.h"
#include "RiscFirmwareI2cCompatV1.h"
#include <stddef.h>
#include <stdint.h>

#define MAX_CLAIMS 12u

typedef struct {
    uint64_t token;
    uint8_t address;
} device_claim;
static device_claim claims[MAX_CLAIMS];
static uint64_t next_token;
static bool started, busy;

static bool claim_device(void *context, uint8_t address, uint64_t *out) {
    (void)context;
    if (out) *out = 0;
    if (!out || !started || busy || address < 0x08u || address > 0x77u ||
        next_token == UINT64_MAX) return false;
    device_claim *empty = NULL;
    for (size_t i = 0; i < MAX_CLAIMS; ++i) {
        if (claims[i].token && claims[i].address == address) return false;
        if (!claims[i].token && !empty) empty = &claims[i];
    }
    if (!empty) return false;
    empty->address = address;
    empty->token = ++next_token;
    *out = empty->token;
    return true;
}

static bool transact(void *context, uint64_t token,
                     const uint8_t *write_bytes, size_t write_length,
                     uint8_t *read_bytes, size_t read_length,
                     uint32_t timeout_ms) {
    (void)context;
    if (!started || busy || !token || (!write_length && !read_length) ||
        write_length > RISC_FW_I2C_COMPAT_V1_MAX_BYTES ||
        read_length > RISC_FW_I2C_COMPAT_V1_MAX_BYTES ||
        (write_length && !write_bytes) || (read_length && !read_bytes) ||
        !timeout_ms || timeout_ms > RISC_FW_I2C_COMPAT_V1_MAX_TIMEOUT_MS)
        return false;
    const device_claim *found = NULL;
    for (size_t i = 0; i < MAX_CLAIMS; ++i)
        if (claims[i].token == token) { found = &claims[i]; break; }
    if (!found) return false;
    busy = true;
    const bool ok = risc_fw_i2c_transact_v1(found->address, write_bytes,
                                            write_length, read_bytes,
                                            read_length, timeout_ms);
    busy = false;
    /* A NACK or timed-out transaction is a failure; no fabricated read or
     * success is returned to board.power.vbus or another dependent provider. */
    return ok;
}

static bool release_device(void *context, uint64_t token) {
    (void)context;
    if (!started || busy || !token) return false;
    for (size_t i = 0; i < MAX_CLAIMS; ++i) {
        if (claims[i].token == token) {
            claims[i].token = 0;
            claims[i].address = 0;
            return true;
        }
    }
    return false;
}

static bool quiesce(void) {
    if (busy) return false;
    for (size_t i = 0; i < MAX_CLAIMS; ++i)
        if (claims[i].token) return false;
    /* No physical ownership to release. The firmware continues to serve
     * touch/battery/expander transactions after this ELF is unmapped. */
    started = false;
    return true;
}

static bool start(const risc_provider_dependency_v1 *dependencies,
                  size_t dependency_count) {
    (void)dependencies;
    if (dependency_count || started || busy) return false;
    for (size_t i = 0; i < MAX_CLAIMS; ++i)
        if (claims[i].token) return false;
    /* Private symbol resolution already proves the firmware supports the
     * compatibility transport. Do not call Wire.begin() or install IDF I2C. */
    started = true;
    return true;
}
static void stop(void) { (void)quiesce(); }

static const risc_i2c_bus_api_v1 bus_api = {
    RISC_I2C_BUS_API_V1, sizeof(risc_i2c_bus_api_v1), NULL,
    claim_device, transact, release_device
};
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "i2c-esp32s3-v2", "i2c.bus", RISC_I2C_BUS_API_V1,
    &bus_api, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : NULL;
}
