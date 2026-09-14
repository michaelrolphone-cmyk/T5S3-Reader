#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define T5_LANGUAGE_API_VERSION 1u
#define T5_LANGUAGE_NAME_MAX 96u

typedef struct {
    uint8_t language_id;
    uint8_t selected;
    char name[T5_LANGUAGE_NAME_MAX];
} t5_language_info_t;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;

    // Languages are returned in the firmware's normal display order.
    uint32_t (*count)(void);
    bool (*read)(uint32_t display_index, t5_language_info_t *info);

    // Applies the language immediately and persists the corresponding firmware
    // setting. Native apps do not access I18N or CrossPointSettings directly.
    bool (*select)(uint8_t language_id);
} t5_language_api_v1;

const t5_language_api_v1 *t5_language_get_api(uint32_t api_version);

#ifdef __cplusplus
}
#endif
