#pragma once
/* Board battery gauge capability. Opaque to the runtime. */
#include "RiscProviderV2.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_BATTERY_GAUGE_API_V1 1u
typedef struct {
    uint16_t millivolts;
    uint8_t percent;
    uint8_t charging;
} risc_battery_sample_v1;
typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    void *context;
    bool (*read)(void *context, risc_battery_sample_v1 *out);
} risc_battery_gauge_api_v1;
#ifdef __cplusplus
}
#endif
