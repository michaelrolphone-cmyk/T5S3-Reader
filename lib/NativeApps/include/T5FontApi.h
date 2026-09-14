#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define T5_FONT_API_VERSION 2u
#define T5_FONT_NAME_MAX 64u
#define T5_FONT_DESCRIPTION_MAX 128u

typedef enum {
    T5_FONT_OK = 0,
    T5_FONT_NETWORK_ERROR = 1,
    T5_FONT_MANIFEST_ERROR = 2,
    T5_FONT_STORAGE_ERROR = 3,
    T5_FONT_CHECKSUM_ERROR = 4,
    T5_FONT_INVALID_FILE = 5,
    T5_FONT_INVALID_INDEX = 6,
    T5_FONT_UNAVAILABLE = 255,
} t5_font_result_t;

typedef struct {
    char name[T5_FONT_NAME_MAX];
    char description[T5_FONT_DESCRIPTION_MAX];
    size_t total_size;
    uint8_t installed;
    uint8_t has_update;
    uint8_t reserved[2];
} t5_font_family_info_t;

typedef struct {
    char name[T5_FONT_NAME_MAX];
    uint8_t builtin;
    uint8_t selected;
    uint8_t reserved[2];
} t5_font_choice_info_t;

typedef void (*t5_font_progress_callback_t)(const char *family_name,
                                             uint32_t file_index,
                                             uint32_t file_count,
                                             size_t downloaded,
                                             size_t total,
                                             void *ctx);

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    t5_font_result_t (*refresh_catalog)(void);
    uint32_t (*family_count)(void);
    bool (*family_info)(uint32_t index, t5_font_family_info_t *out);
    t5_font_result_t (*install_family)(uint32_t index, t5_font_progress_callback_t callback, void *ctx);
    t5_font_result_t (*delete_family)(uint32_t index);
    uint32_t (*choice_count)(void);
    bool (*choice_info)(uint32_t index, t5_font_choice_info_t *out);
    t5_font_result_t (*select_choice)(uint32_t index);
} t5_font_api_v1;

const t5_font_api_v1 *t5_font_get_api(uint32_t api_version);

#ifdef __cplusplus
}
#endif
