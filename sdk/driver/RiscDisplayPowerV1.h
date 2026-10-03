#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Exclusive panel power lease; caller must stop scan/DMA before release.
 * False acquire may return a retained grant after uncertain power writes.
 * False release retains the grant and provider dependencies; never unload. */
typedef struct risc_display_power_api_v1 {
    uint32_t api_version, struct_size;
    void *context;
    bool (*acquire)(void *context, uint64_t *grant);
    bool (*release)(void *context, uint64_t grant);
} risc_display_power_api_v1;
#ifdef __cplusplus
}
#endif
