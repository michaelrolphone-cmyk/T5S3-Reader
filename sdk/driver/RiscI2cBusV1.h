#pragma once
/* Provider-to-provider I2C contract. This is NOT a firmware I2C bridge: an
 * installable bus-controller ELF owns the peripheral, locks, clock and pins.
 * Capability identifiers remain opaque to the runtime. */
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
     * Buffers are borrowed only until the synchronous call returns. */
    bool (*transact)(void *context, uint64_t claim,
                     const uint8_t *write_bytes, size_t write_length,
                     uint8_t *read_bytes, size_t read_length,
                     uint32_t timeout_ms);
    /* Returns true only when all bus operations for this claim have drained. */
    bool (*release_device)(void *context, uint64_t claim);
} risc_i2c_bus_api_v1;
#ifdef __cplusplus
}
#endif
