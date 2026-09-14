#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define T5_TIME_ZONE_API_VERSION 1u
#define T5_TIME_ZONE_NAME_MAX 64u
#define T5_TIME_ZONE_ID_MAX 40u

typedef struct {
    char name[T5_TIME_ZONE_NAME_MAX];
    uint8_t selected;
    uint8_t reserved[3];
} t5_time_zone_region_info_t;

typedef struct {
    char id[T5_TIME_ZONE_ID_MAX];
    char name[T5_TIME_ZONE_NAME_MAX];
    uint8_t selected;
    uint8_t reserved[3];
} t5_time_zone_city_info_t;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    uint32_t (*region_count)(void);
    bool (*region_info)(uint32_t region, t5_time_zone_region_info_t *out);
    uint32_t (*city_count)(uint32_t region);
    bool (*city_info)(uint32_t region, uint32_t city, t5_time_zone_city_info_t *out);
    bool (*select_city)(uint32_t region, uint32_t city);
} t5_time_zone_api_v1;

const t5_time_zone_api_v1 *t5_time_zone_get_api(uint32_t api_version);

#ifdef __cplusplus
}
#endif
