#pragma once
/* Hardware-blind provider ABI. Capability identifiers and interface pointers
 * are opaque to RiscRTE; a provider owns its physical implementation. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
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
     * software-grant release as proof of quiescence. */
    bool (*quiesce)(void);
    /* Optional, append-only diagnostic hook. Called synchronously by the
     * generic loader after start() rejects, BEFORE quiesce()/stop()/unmap().
     * The provider writes a NUL-terminated, bounded diagnostic into output;
     * it must not include device RX/TX data or alter hardware state. */
#ifdef __cplusplus
    bool (*last_start_failure)(char *output, size_t capacity) = nullptr;
#else
    bool (*last_start_failure)(char *output, size_t capacity);
#endif
} risc_driver_v2;

/* Keep old ABI-v2 providers valid, including those without either extension. */
#define RISC_DRIVER_V2_BASE_SIZE offsetof(risc_driver_v2, quiesce)
#define RISC_DRIVER_V2_QUIESCE_SIZE \
    (offsetof(risc_driver_v2, quiesce) + sizeof(((risc_driver_v2 *)0)->quiesce))
#define RISC_DRIVER_V2_DIAGNOSTIC_SIZE \
    (offsetof(risc_driver_v2, last_start_failure) + sizeof(((risc_driver_v2 *)0)->last_start_failure))

typedef const risc_driver_v2 *(*risc_driver_get_v2_fn)(uint32_t abi);
const risc_driver_v2 *t5_driver_get(uint32_t abi);
#ifdef __cplusplus
}
#endif
