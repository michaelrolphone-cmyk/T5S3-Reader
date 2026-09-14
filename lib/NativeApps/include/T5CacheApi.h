#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define T5_CACHE_API_VERSION 1u

typedef struct {
    uint32_t removed_count;
    uint32_t failed_count;
    uint8_t directory_available;
    uint8_t reserved[3];
} t5_cache_clear_result_t;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;

    // Deletes firmware-owned reading cache directories only (epub_* and xtc_*)
    // beneath /.crosspoint and returns aggregate results to the ELF.
    bool (*clear_reading_cache)(t5_cache_clear_result_t *result);
} t5_cache_api_v1;

const t5_cache_api_v1 *t5_cache_get_api(uint32_t api_version);

#ifdef __cplusplus
}
#endif
