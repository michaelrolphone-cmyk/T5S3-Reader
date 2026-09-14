#include "T5AppApi.h"
#include "T5SystemUiApi.h"
#include "T5TimecardApi.h"

#define SELF_PATH "/sd/Apps/timecard.elf"
#define DAY_COUNT 7
#define PUNCH_COUNT 4
#define WEEK_HISTORY 20
#define WEEK_ITEM_COUNT (DAY_COUNT + PUNCH_COUNT)
#define STATUS_CAP 128
#define COOKIE_MAGIC 0x54u

static const t5_app_api_v1 *g_app;
static const t5_system_ui_api_v1 *g_ui;
static const t5_timecard_api_v1 *g_tc;

static uint8_t g_screen = T5_TIMECARD_WEEK_LIST;
static int32_t g_week_offset = 0;
static int32_t g_selected = 0;
static int32_t g_editing_ymd = 0;
static char g_status[STATUS_CAP];

static void copy_text(char *dst, size_t capacity, const char *src) {
    size_t i = 0;
    if (!dst || capacity == 0) return;
    if (!src) src = "";
    while (src[i] && i + 1 < capacity) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static void append_text(char *dst, size_t capacity, const char *src) {
    size_t used = 0;
    size_t i = 0;
    if (!dst || capacity == 0 || !src) return;
    while (used < capacity && dst[used]) ++used;
    if (used >= capacity) return;
    while (src[i] && used + 1 < capacity) {
        dst[used++] = src[i++];
    }
    dst[used] = '\0';
}

static void set_status(const char *text) {
    copy_text(g_status, sizeof(g_status), text);
}

static void set_punch_status(uint8_t punch, int16_t minutes) {
    char label[64];
    char time_text[24];
    g_status[0] = '\0';
    if (!g_tc->punch_label(punch, label, sizeof(label))) copy_text(label, sizeof(label), "Time");
    if (!g_tc->format_ampm(minutes, time_text, sizeof(time_text))) copy_text(time_text, sizeof(time_text), "");
    append_text(g_status, sizeof(g_status), label);
    append_text(g_status, sizeof(g_status), "  ");
    append_text(g_status, sizeof(g_status), time_text);
}

static bool day_has_any(const t5_timecard_day_t *day) {
    int i;
    if (!day) return false;
    for (i = 0; i < PUNCH_COUNT; ++i) {
        if (day->punches[i] >= 0) return true;
    }
    return false;
}

static int32_t item_count(void) {
    if (g_screen == T5_TIMECARD_WEEK_LIST) return WEEK_HISTORY;
    if (g_screen == T5_TIMECARD_DAY) return PUNCH_COUNT;
    return WEEK_ITEM_COUNT;
}

static int32_t selected_for_today(void) {
    const int32_t today = g_tc->today_ymd();
    const int32_t sunday = g_tc->sunday_ymd(0);
    int32_t i;
    for (i = 0; i < DAY_COUNT; ++i) {
        if (g_tc->add_days(sunday, i) == today) return i;
    }
    return 0;
}

static void render(void) {
    g_tc->render(g_screen, g_week_offset, g_selected, g_editing_ymd, g_status);
}

static void move_selection(int32_t delta) {
    const int32_t count = item_count();
    if (count <= 0) return;
    g_selected += delta;
    if (g_selected < 0) g_selected = count - 1;
    else if (g_selected >= count) g_selected = 0;
}

static void open_week(int32_t offset) {
    g_week_offset = offset;
    g_screen = T5_TIMECARD_WEEK;
    g_selected = offset == 0 ? selected_for_today() : 0;
    g_editing_ymd = 0;
    set_status("Open a day, or punch below");
}

static void open_day(int32_t ymd) {
    t5_timecard_day_t day;
    g_tc->reload();
    g_editing_ymd = ymd;
    g_screen = T5_TIMECARD_DAY;
    g_selected = 0;
    if (g_tc->get_day(ymd, &day) && day_has_any(&day)) set_status("Confirm a row to edit");
    else set_status("Confirm a row to set time");
}

static void punch_today(uint8_t punch) {
    const int32_t today = g_tc->today_ymd();
    const int16_t minutes = (int16_t)g_tc->current_minutes();
    if (!g_tc->set_punch(today, punch, minutes)) {
        set_status("Could not save punch");
        return;
    }
    g_week_offset = 0;
    g_screen = T5_TIMECARD_WEEK;
    g_editing_ymd = 0;
    g_selected = selected_for_today();
    set_punch_status(punch, minutes);
}

static uint64_t make_edit_cookie(int32_t ymd, uint8_t punch, int32_t week_offset) {
    uint64_t value = (uint32_t)ymd;
    value |= ((uint64_t)punch) << 32;
    value |= ((uint64_t)(uint16_t)(int16_t)week_offset) << 40;
    value |= ((uint64_t)COOKIE_MAGIC) << 56;
    return value;
}

static bool decode_edit_cookie(uint64_t value, int32_t *ymd, uint8_t *punch, int32_t *week_offset) {
    if ((uint8_t)(value >> 56) != COOKIE_MAGIC) return false;
    if (ymd) *ymd = (int32_t)(uint32_t)value;
    if (punch) *punch = (uint8_t)(value >> 32);
    if (week_offset) *week_offset = (int32_t)(int16_t)(uint16_t)(value >> 40);
    return true;
}

static bool request_edit(void) {
    t5_timecard_day_t day;
    char title[64];
    char initial[24];
    int16_t current = -1;
    const uint8_t punch = (uint8_t)g_selected;

    if (g_screen != T5_TIMECARD_DAY || punch >= PUNCH_COUNT) return false;
    if (g_tc->get_day(g_editing_ymd, &day)) current = day.punches[punch];
    if (!g_tc->punch_label(punch, title, sizeof(title))) copy_text(title, sizeof(title), "Edit time");
    initial[0] = '\0';
    if (current >= 0) g_tc->format_ampm(current, initial, sizeof(initial));

    if (!g_ui->keyboard_request(SELF_PATH, title, initial, 12, T5_SYSTEM_KEYBOARD_TEXT,
                                make_edit_cookie(g_editing_ymd, punch, g_week_offset))) {
        set_status("Keyboard unavailable");
        return false;
    }
    return true;
}

static bool activate(void) {
    if (g_screen == T5_TIMECARD_WEEK_LIST) {
        open_week(-g_selected);
        return false;
    }
    if (g_screen == T5_TIMECARD_DAY) {
        return request_edit();
    }
    if (g_selected < DAY_COUNT) {
        const int32_t sunday = g_tc->sunday_ymd(g_week_offset);
        open_day(g_tc->add_days(sunday, g_selected));
    } else {
        punch_today((uint8_t)(g_selected - DAY_COUNT));
    }
    return false;
}

static bool consume_keyboard_result(void) {
    char text[64];
    bool cancelled = false;
    uint64_t cookie = 0;
    int32_t ymd = 0;
    int32_t week_offset = 0;
    uint8_t punch = 0;
    int16_t minutes = -1;
    t5_timecard_day_t day;

    if (!g_ui->keyboard_take_result(text, sizeof(text), &cancelled, &cookie)) return false;
    if (!decode_edit_cookie(cookie, &ymd, &punch, &week_offset) || punch >= PUNCH_COUNT || ymd < 19700101) {
        set_status("Keyboard result ignored");
        return true;
    }

    g_tc->reload();
    g_screen = T5_TIMECARD_DAY;
    g_week_offset = week_offset;
    g_selected = punch;
    g_editing_ymd = ymd;

    if (cancelled) {
        if (g_tc->get_day(ymd, &day) && day_has_any(&day)) set_status("Confirm a row to edit");
        else set_status("Confirm a row to set time");
        return true;
    }

    if (!g_tc->set_punch_text(ymd, punch, text, &minutes)) {
        set_status("Edit time");
        return true;
    }
    set_punch_status(punch, minutes);
    return true;
}

static bool has_required_apis(void) {
    const size_t app_required = offsetof(t5_app_api_v1, set_back_exits_app) + sizeof(g_app->set_back_exits_app);
    const size_t ui_required = offsetof(t5_system_ui_api_v1, navigate_home) + sizeof(g_ui->navigate_home);
    const size_t tc_required = offsetof(t5_timecard_api_v1, touch) + sizeof(g_tc->touch);
    return g_app && g_app->struct_size >= app_required && g_app->poll && g_app->set_back_exits_app &&
           g_ui && g_ui->struct_size >= ui_required && g_ui->keyboard_request && g_ui->keyboard_take_result &&
           g_ui->navigate_home && g_tc && g_tc->struct_size >= tc_required && g_tc->reload && g_tc->today_ymd &&
           g_tc->current_minutes && g_tc->sunday_ymd && g_tc->add_days && g_tc->get_day && g_tc->set_punch &&
           g_tc->set_punch_text && g_tc->punch_label && g_tc->format_ampm && g_tc->render && g_tc->touch;
}

__attribute__((visibility("default"))) void app_main(void) {
    t5_app_input_t input;
    bool armed = false;

    g_app = t5_app_get_api(T5_APP_ABI_VERSION);
    g_ui = t5_system_ui_get_api(T5_SYSTEM_UI_API_VERSION);
    g_tc = t5_timecard_get_api(T5_TIMECARD_API_VERSION);
    if (!has_required_apis()) return;

    g_app->set_back_exits_app(false);
    g_tc->reload();

    if (!consume_keyboard_result()) {
        g_screen = T5_TIMECARD_WEEK_LIST;
        g_week_offset = 0;
        g_selected = 0;
        g_editing_ymd = 0;
        set_status("Select a week");
    }
    render();

    while (g_app->poll(&input, 20)) {
        const uint32_t buttons = input.buttons;
        if (input.exit_requested) return;

        if (buttons == 0 && !input.tapped) {
            armed = true;
            continue;
        }
        if (!armed) continue;
        armed = false;

        if (input.tapped) {
            int32_t touched = g_selected;
            const uint8_t hit = g_tc->touch(g_screen, input.touch_x, input.touch_y, &touched);
            if (hit == T5_TIMECARD_TOUCH_HEADER) {
                g_screen = T5_TIMECARD_WEEK_LIST;
                g_selected = -g_week_offset;
                if (g_selected < 0) g_selected = 0;
                if (g_selected >= WEEK_HISTORY) g_selected = WEEK_HISTORY - 1;
                g_editing_ymd = 0;
                set_status("Select a week");
                render();
                continue;
            }
            if (hit == T5_TIMECARD_TOUCH_ITEM) {
                g_selected = touched;
                if (activate()) return;
                render();
            }
            continue;
        }

        if (buttons & T5_APP_BUTTON_BACK) {
            if (g_screen == T5_TIMECARD_DAY) {
                g_screen = T5_TIMECARD_WEEK;
                g_editing_ymd = 0;
                g_selected = 0;
                set_status("Open a day, or punch below");
                render();
                continue;
            }
            if (g_screen == T5_TIMECARD_WEEK) {
                g_screen = T5_TIMECARD_WEEK_LIST;
                g_selected = -g_week_offset;
                if (g_selected < 0) g_selected = 0;
                if (g_selected >= WEEK_HISTORY) g_selected = WEEK_HISTORY - 1;
                set_status("Select a week");
                render();
                continue;
            }
            g_ui->navigate_home();
            return;
        }

        if (buttons & T5_APP_BUTTON_CONFIRM) {
            if (activate()) return;
            render();
            continue;
        }

        if (buttons & T5_APP_BUTTON_UP) {
            move_selection(-1);
            render();
            continue;
        }

        if (buttons & T5_APP_BUTTON_DOWN) {
            move_selection(1);
            render();
            continue;
        }
    }
}
