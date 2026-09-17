#pragma once
/* Hardware-blind provider ABI. The core treats capability IDs and the
 * capability interface pointers as opaque. The provider owns hardware. */
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
} risc_driver_v2;

typedef const risc_driver_v2 *(*risc_driver_get_v2_fn)(uint32_t abi);
/* The ABI-v1 loader must not activate this ABI. The generic dependency-aware
 * loader validates the provider's manifest before calling this symbol. */
const risc_driver_v2 *t5_driver_get(uint32_t abi);
#ifdef __cplusplus
}
#endif
