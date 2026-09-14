#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define T5_SYSTEM_API_VERSION 1u

typedef struct {
    int16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t weekday;   // 0=Sunday ... 6=Saturday
    uint16_t yearday;  // 0-based
} t5_local_datetime_t;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;

    // Returns the firmware's timezone-adjusted local wall clock. Native apps own
    // all higher-level calendar/business logic built from this primitive.
    bool (*local_datetime)(t5_local_datetime_t *value);
} t5_system_api_v1;

const t5_system_api_v1 *t5_system_get_api(uint32_t api_version);

#ifdef __cplusplus
}
#endif
