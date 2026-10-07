#pragma once
/* Stable provider-to-provider I2C contract. Upstream drivers depend on
 * i2c.bus, never a firmware peripheral function or a particular SPI/I2C
 * implementation. The current i2c-esp32s3-v2 ELF delegates transactions to
 * firmware-owned, shared-lock Wire/I2C0 through a private compatibility ABI.
 * Once I2C ownership moves entirely into its ELF, this public ABI and its
 * upstream consumers remain unchanged. Capability IDs are opaque to runtime.
 */
#include "RiscProviderV2.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_I2C_BUS_API_V1 1u
typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    void *context;
    /* Only one device owner for each 7-bit address; a rejected claim MUST
     * return no token and leave hardware unchanged. Never reuse a token. */
    bool (*claim_device)(void *context, uint8_t address, uint64_t *claim);
    /* Both phases are one serialized bus transaction. If read_length > 0,
     * the bus MUST issue a repeated START without a STOP after write_bytes.
     * A false result means caller MUST NOT assume a register write succeeded.
     * timeout_ms is a total admission/transfer budget, not a fresh timeout
     * for each nested lock. Forward only the remaining budget to the physical
     * transport. Tick rounding/scheduler latency can add one scheduler tick.
     * Buffers are borrowed only until the synchronous call returns. */
    bool (*transact)(void *context, uint64_t claim,
                     const uint8_t *write_bytes, size_t write_length,
                     uint8_t *read_bytes, size_t read_length,
                     uint32_t timeout_ms);
    /* Returns true only when all bus operations for this claim have drained. */
    bool (*release_device)(void *context, uint64_t claim);
} risc_i2c_bus_api_v1;

/* Optional append-only conformance suffix. The original API-1 prefix above is
 * frozen: legacy providers/consumers retain its layout. struct_size includes
 * this suffix only when a provider explicitly implements these guarantees.
 * Size alone never identifies this extension; validate tag AND version AND
 * all requested flags before the first claim, GPIO access or transaction.
 * This is provider-declared behavior, not package trust or authorization. */
#define RISC_I2C_BUS_CONTRACT_TAG 0x49324353u /* I2CS */
#define RISC_I2C_BUS_CONTRACT_V1 1u
#define RISC_I2C_BUS_SERIALIZED (1u << 0)
#define RISC_I2C_BUS_TOTAL_DEADLINE (1u << 1)
#define RISC_I2C_BUS_RETAINED_RELEASE (1u << 2)
#define RISC_I2C_BUS_SAFE_CONTRACT_FLAGS \
    (RISC_I2C_BUS_SERIALIZED | RISC_I2C_BUS_TOTAL_DEADLINE | RISC_I2C_BUS_RETAINED_RELEASE)
typedef struct {
    risc_i2c_bus_api_v1 base;
    uint32_t contract_tag;
    uint32_t contract_version;
    /* SERIALIZED: claims, entire repeated-START transfers, releases and
     * lifecycle cannot interleave, including reentry; contention may fail.
     * TOTAL_DEADLINE: one bounded admission/transfer budget, checked after
     * fixed safety cleanup. Scheduler latency/cleanup may overrun the budget
     * but an expired call never returns success; no unbounded stretch/retry.
     * RETAINED_RELEASE: false keeps the exact claim valid and provider pinned;
     * true means drained; quiescence refuses live claims/unsafe hardware. */
    uint32_t contract_flags;
} risc_i2c_bus_contract_v1;
static inline bool risc_i2c_bus_has_safe_contract(const risc_i2c_bus_api_v1 *api) {
    if (!api || api->api_version != RISC_I2C_BUS_API_V1 ||
        api->struct_size < sizeof(risc_i2c_bus_contract_v1)) return false;
    const risc_i2c_bus_contract_v1 *contract = (const risc_i2c_bus_contract_v1 *)api;
    return contract->contract_tag == RISC_I2C_BUS_CONTRACT_TAG &&
           contract->contract_version == RISC_I2C_BUS_CONTRACT_V1 &&
           (contract->contract_flags & RISC_I2C_BUS_SAFE_CONTRACT_FLAGS) ==
               RISC_I2C_BUS_SAFE_CONTRACT_FLAGS &&
           api->claim_device && api->transact && api->release_device;
}
#ifdef __cplusplus
}
#endif
