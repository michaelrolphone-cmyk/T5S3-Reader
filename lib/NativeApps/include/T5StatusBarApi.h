#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define T5_STATUS_BAR_API_VERSION 1u
#define T5_STATUS_BAR_ITEM_COUNT 7u
#define T5_STATUS_BAR_LABEL_MAX 64u
#define T5_STATUS_BAR_VALUE_MAX 64u

typedef struct {
    char label[T5_STATUS_BAR_LABEL_MAX];
    char value[T5_STATUS_BAR_VALUE_MAX];
} t5_status_bar_item_t;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    uint32_t (*item_count)(void);
    bool (*item_get)(uint32_t index, t5_status_bar_item_t *out);
    bool (*item_activate)(uint32_t index);
} t5_status_bar_api_v1;

const t5_status_bar_api_v1 *t5_status_bar_get_api(uint32_t api_version);

#ifdef __cplusplus
}
#endif
