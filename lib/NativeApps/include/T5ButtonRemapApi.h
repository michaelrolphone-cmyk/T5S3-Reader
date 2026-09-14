#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define T5_BUTTON_REMAP_API_VERSION 1u
#define T5_BUTTON_REMAP_ROLE_COUNT 4u

typedef enum {
    T5_BUTTON_REMAP_BACK = 0,
    T5_BUTTON_REMAP_CONFIRM = 1,
    T5_BUTTON_REMAP_LEFT = 2,
    T5_BUTTON_REMAP_RIGHT = 3,
} t5_button_remap_role_t;

typedef struct {
    uint8_t role_to_hardware[T5_BUTTON_REMAP_ROLE_COUNT];
} t5_button_remap_mapping_t;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    bool (*read_mapping)(t5_button_remap_mapping_t *out);
    bool (*apply_mapping)(const t5_button_remap_mapping_t *mapping);
    bool (*reset_defaults)(void);
} t5_button_remap_api_v1;

const t5_button_remap_api_v1 *t5_button_remap_get_api(uint32_t api_version);

#ifdef __cplusplus
}
#endif
