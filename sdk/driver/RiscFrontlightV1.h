#pragma once
/* Board frontlight capability. Opaque to the runtime. Level is 0..maximum. */
#include "RiscProviderV2.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_FRONTLIGHT_API_V1 1u
typedef struct risc_frontlight_api_v1 {
    uint32_t api_version;
    uint32_t struct_size;
    void *context;
    bool (*set_level)(void *context, uint16_t level, uint16_t maximum);
    bool (*get_level)(void *context, uint16_t *level, uint16_t *maximum);
} risc_frontlight_api_v1;
#ifdef __cplusplus
}
#endif
