#pragma once
/* Stable provider-to-provider SPI sessions. Peripheral protocols live in their
 * own ELFs. Sessions are synchronous and task-owned: finish on the same task.
 * A failed transfer has unknown device effects and must not be retried blindly.
 * Claims and sessions are generation tokens, never pointers or pin ownership.
 */
#include "RiscProviderV2.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_SPI_BUS_API_V1 1u
#define RISC_SPI_TRANSFER_MAX 4096u
#define RISC_SPI_SESSION_MAX_BYTES 65536u
#define RISC_SPI_SESSION_MAX_MS 1000u
typedef struct {
    uint32_t api_version, struct_size;
    void *context;
    bool (*claim_device)(void *context, uint8_t chip_select, uint64_t *claim);
    /* Mode 0, MSB first. Admission is bounded by the shared controller's
     * 30-second fault-retention deadline; then a session has at most 1 second
     * and 64 KiB. Expiration rejects further transfers but still permits end.
     * An unquiesced physical controller retains ownership until manual reboot.
     * selected=false permits clocks with all chip selects inactive (SD init).
     */
    bool (*begin)(void *context, uint64_t claim, uint32_t frequency_hz,
                  bool selected, uint64_t *session);
    bool (*select)(void *context, uint64_t session, bool selected);
    /* NULL tx sends 0xff; NULL rx discards. Buffers borrowed until return. */
    bool (*transfer)(void *context, uint64_t session, const uint8_t *tx,
                     uint8_t *rx, size_t bytes);
    bool (*end)(void *context, uint64_t session);
    bool (*release_device)(void *context, uint64_t claim);
} risc_spi_bus_api_v1;
#ifdef __cplusplus
}
#endif
