#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define T5_DRIVER_MANAGER_API_VERSION 1u
#define T5_DRIVER_ID_MAX 64u
#define T5_DRIVER_VERSION_MAX 32u
#define T5_DRIVER_CAPABILITY_MAX 64u

typedef struct {
    char id[T5_DRIVER_ID_MAX];
    char version[T5_DRIVER_VERSION_MAX];
    char capability[T5_DRIVER_CAPABILITY_MAX];
    uint32_t size_bytes;
} t5_driver_catalog_entry_t;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    bool (*catalog_refresh)(void);
    uint32_t (*catalog_count)(void);
    bool (*catalog_get)(uint32_t index, t5_driver_catalog_entry_t *entry);
    bool (*installed_version_get)(const char *id, char *version, size_t capacity);
    bool (*install)(uint32_t index);
} t5_driver_manager_api_v1;

const t5_driver_manager_api_v1 *t5_driver_manager_get_api(uint32_t version);

#ifdef __cplusplus
}
#endif
