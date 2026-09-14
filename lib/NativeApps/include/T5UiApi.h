#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define T5_UI_API_VERSION 1u
#define T5_UI_MAX_COLUMNS 6u
#define T5_UI_HIT_NONE (-1)
#define T5_UI_HIT_HEADER (-2)
#define T5_UI_LIST_HIGHLIGHT_VALUE (1u << 0)
#define T5_UI_TABLE_ROW_FULL_WIDTH (1u << 0)

typedef enum {
    T5_UI_EVENT_NONE = 0,
    T5_UI_EVENT_PREVIOUS = 1,
    T5_UI_EVENT_NEXT = 2,
    T5_UI_EVENT_CONFIRM = 3,
    T5_UI_EVENT_BACK = 4,
    T5_UI_EVENT_TAP = 5,
    T5_UI_EVENT_EXIT = 6,
} t5_ui_event_type_t;

typedef struct {
    uint8_t type;
    int16_t touch_x;
    int16_t touch_y;
} t5_ui_event_t;

typedef struct {
    const char *title;
    const char *subtitle;
    const char *status;
    const char *back_label;
    const char *confirm_label;
    const char *previous_label;
    const char *next_label;
} t5_ui_chrome_t;

typedef struct {
    const char *title;
    const char *subtitle;
    const char *value;
    uint8_t flags;
} t5_ui_list_row_t;

typedef struct {
    const char *title;
    uint8_t weight;
} t5_ui_table_column_t;

typedef struct {
    const char *cells[T5_UI_MAX_COLUMNS];
    uint8_t flags;
} t5_ui_table_row_t;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;

    // Render through the active firmware theme, fonts, metrics and hardware
    // button-label mapping. All pointed-to strings only need to remain valid for
    // the duration of the call.
    void (*render_list)(const t5_ui_chrome_t *chrome,
                        const t5_ui_list_row_t *rows,
                        uint32_t row_count,
                        int32_t selected_index);
    void (*render_table)(const t5_ui_chrome_t *chrome,
                         const t5_ui_table_column_t *columns,
                         uint32_t column_count,
                         const t5_ui_table_row_t *rows,
                         uint32_t row_count,
                         int32_t selected_index);

    // Hit-test against the most recently rendered list/table. Returns a row
    // index, T5_UI_HIT_HEADER, or T5_UI_HIT_NONE.
    int32_t (*hit_test)(int16_t x, int16_t y);

    // Firmware navigation semantics: Confirm fires on release; previous/next use
    // ButtonNavigator (Up/Left and Down/Right, including hold-repeat); touch
    // button hints honor the user's front-button mapping. Power/Home report EXIT.
    bool (*poll_event)(t5_ui_event_t *event, uint32_t wait_ms);

    int32_t (*next_index)(int32_t current_index, uint32_t item_count);
    int32_t (*previous_index)(int32_t current_index, uint32_t item_count);
} t5_ui_api_v1;

const t5_ui_api_v1 *t5_ui_get_api(uint32_t api_version);

#ifdef __cplusplus
}
#endif
