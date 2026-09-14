#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define T5_TIMECARD_API_VERSION 1u
#define T5_TIMECARD_PUNCH_COUNT 4u
#define T5_TIMECARD_STATUS_MAX 128u

typedef enum {
    T5_TIMECARD_WEEK_LIST = 0,
    T5_TIMECARD_WEEK = 1,
    T5_TIMECARD_DAY = 2,
} t5_timecard_screen_t;

typedef enum {
    T5_TIMECARD_CLOCK_IN = 0,
    T5_TIMECARD_LUNCH_START = 1,
    T5_TIMECARD_LUNCH_END = 2,
    T5_TIMECARD_CLOCK_OUT = 3,
} t5_timecard_punch_t;

typedef enum {
    T5_TIMECARD_TOUCH_NONE = 0,
    T5_TIMECARD_TOUCH_HEADER = 1,
    T5_TIMECARD_TOUCH_ITEM = 2,
} t5_timecard_touch_result_t;

typedef struct {
    int32_t ymd;
    int16_t punches[T5_TIMECARD_PUNCH_COUNT];
} t5_timecard_day_t;

typedef struct {
    uint8_t screen;
    uint8_t reserved[3];
    int32_t week_offset;
    int32_t selected_index;
    int32_t editing_ymd;
    char status[T5_TIMECARD_STATUS_MAX];
} t5_timecard_resume_t;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;

    // Shared persistence/date service. Firmware TimecardActivity and native
    // timecard.elf both use TimecardStore, which persists to
    // /.crosspoint/timecard.json on the SD card.
    void (*reload)(void);
    int32_t (*today_ymd)(void);
    int32_t (*current_minutes)(void);
    int32_t (*sunday_ymd)(int32_t week_offset);
    int32_t (*add_days)(int32_t ymd, int32_t days);
    bool (*get_day)(int32_t ymd, t5_timecard_day_t *day);
    bool (*set_punch)(int32_t ymd, uint8_t punch, int16_t minutes_from_midnight);
    bool (*punch_label)(uint8_t punch, char *label, size_t capacity);
    bool (*format_ampm)(int16_t minutes_from_midnight, char *text, size_t capacity);

    // Exact firmware-themed rendering and hit testing from the same metrics and
    // typography used by TimecardActivity. The ELF owns navigation state.
    void (*render)(uint8_t screen, int32_t week_offset, int32_t selected_index,
                   int32_t editing_ymd, const char *status);
    uint8_t (*touch)(uint8_t screen, int16_t x, int16_t y, int32_t *selected_index);

    // Time editing temporarily uses the existing firmware keyboard. The resume
    // path is normally /sd/Apps/timecard.elf. After the keyboard closes the host
    // relaunches it and take_resume() restores the Day screen and selected row.
    bool (*request_edit)(int32_t ymd, uint8_t punch, int32_t week_offset, const char *resume_path);
    bool (*take_resume)(t5_timecard_resume_t *resume);
} t5_timecard_api_v1;

// Separate versioned service so application-specific capabilities do not bloat
// or destabilize the generic t5_app_api_v1 layout.
const t5_timecard_api_v1 *t5_timecard_get_api(uint32_t api_version);

#ifdef __cplusplus
}
#endif
