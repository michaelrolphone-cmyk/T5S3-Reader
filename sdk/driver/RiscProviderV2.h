#pragma once
/* Hardware-blind provider ABI. Capability identifiers and interface pointers
 * are opaque to RiscRTE; a provider owns its physical implementation. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "RiscStreamProviderV1.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_PROVIDER_DRIVER_ABI_V2 2u

typedef struct {
    const char *capability_id;
    uint32_t api_version;
    const void *api;
} risc_provider_dependency_v1;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    const char *driver_id;
    const char *capability_id;
    uint32_t capability_api;
    const void *capability;
    bool (*start)(const risc_provider_dependency_v1 *dependencies, size_t count);
    void (*stop)(void);
    /* Optional, append-only ABI-v2 extension. Returns true ONLY after all
     * physical sessions, asynchronous callbacks, DMA and worker tasks have
     * ceased and stop() cannot fail. A false result keeps the ELF mapped and
     * its dependency providers pinned. The runtime MUST NOT interpret a
     * software-grant release as proof of quiescence. Existing v2 modules
     * lacking this member retain legacy stop behavior; hardware providers
     * MUST implement it before becoming installable. */
    bool (*quiesce)(void);
} risc_driver_v2;

/* Optional descriptor suffix. Existing risc_driver_v2 layouts stay unchanged.
 * Set driver.struct_size to sizeof(risc_driver_streams_v2). bind_streams runs
 * before start and may retain the host table through stop. A provider using
 * this extension MUST implement quiesce. Stream authority is revoked before
 * quiesce, including failed activation; teardown must tolerate CLOSED/DENIED.
 * Only the loader grants consumer access separately through capability policy.
 */
typedef struct {
    risc_driver_v2 driver;
    bool (*bind_streams)(const risc_stream_provider_v1 *host);
} risc_driver_streams_v2;

/* Minimum accepted ABI-v2 struct ends before the optional quiesce pointer. */
#define RISC_DRIVER_V2_BASE_SIZE offsetof(risc_driver_v2, quiesce)

typedef const risc_driver_v2 *(*risc_driver_get_v2_fn)(uint32_t abi);
/* ABI-v1 loader must not activate this ABI. The generic dependency-aware
 * loader validates the package and manifest before invoking this symbol. */
const risc_driver_v2 *t5_driver_get(uint32_t abi);
#ifdef __cplusplus
}
#endif
