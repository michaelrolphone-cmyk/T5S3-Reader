#include "T5AppApi.h"
#include "T5StorageApi.h"
#include "T5SystemApi.h"
#include "T5SystemUiApi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DAY_COUNT 7
#define PUNCH_COUNT 4
#define WEEK_HISTORY 20
#define WEEK_ITEM_COUNT (DAY_COUNT + PUNCH_COUNT)
#define MAX_DAYS 400
#define JSON_CAPACITY 49152
#define STATUS_CAP 128
#define COOKIE_MAGIC 0x54u
#define STORE_PATH "/sd/.crosspoint/timecard.json"

#define SCREEN_WEEK_LIST 0u
#define SCREEN_WEEK 1u
#define SCREEN_DAY 2u

#define TOUCH_NONE 0u
#define TOUCH_HEADER 1u
#define TOUCH_ITEM 2u

#define PUNCH_CLOCK_IN 0u
#define PUNCH_LUNCH_START 1u
#define PUNCH_LUNCH_END 2u
#define PUNCH_CLOCK_OUT 3u

typedef struct {
    int32_t ymd;
    int16_t punches[PUNCH_COUNT];
} timecard_day_t;

typedef struct {
    int32_t year;
    int32_t month;
    int32_t day;
} civil_date_t;

static const t5_app_api_v1 *g_app;
static const t5_storage_api_v1 *g_storage;
static const t5_system_api_v1 *g_system;
static const t5_system_ui_api_v1 *g_ui;

static timecard_day_t g_days[MAX_DAYS];
static int32_t g_day_count;
static char g_json[JSON_CAPACITY];

static uint8_t g_screen = SCREEN_WEEK_LIST;
static int32_t g_week_offset;
static int32_t g_selected;
static int32_t g_editing_ymd;
static char g_status[STATUS_CAP];

static size_t text_length(const char *s) {
    size_t n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
}

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
    size_t used = text_length(dst);
    size_t i = 0;
    if (!dst || !src || used >= capacity) return;
    while (src[i] && used + 1 < capacity) dst[used++] = src[i++];
    dst[used] = '\0';
}

static void append_char(char *dst, size_t capacity, char ch) {
    size_t used = text_length(dst);
    if (!dst || used + 1 >= capacity) return;
    dst[used] = ch;
    dst[used + 1] = '\0';
}

static void append_uint(char *dst, size_t capacity, uint32_t value) {
    char reverse[12];
    size_t count = 0;
    if (value == 0) {
        append_char(dst, capacity, '0');
        return;
    }
    while (value && count < sizeof(reverse)) {
        uint32_t quotient = value / 10u;
        reverse[count++] = (char)('0' + value - quotient * 10u);
        value = quotient;
    }
    while (count) append_char(dst, capacity, reverse[--count]);
}

static void append_two_digits(char *dst, size_t capacity, uint32_t value) {
    append_char(dst, capacity, (char)('0' + (value / 10u) % 10u));
    append_char(dst, capacity, (char)('0' + value % 10u));
}

static void set_status(const char *text) {
    copy_text(g_status, sizeof(g_status), text);
}

static civil_date_t split_ymd(int32_t ymd) {
    civil_date_t out;
    out.year = ymd / 10000;
    ymd -= out.year * 10000;
    out.month = ymd / 100;
    out.day = ymd - out.month * 100;
    return out;
}

static int32_t make_ymd(int32_t year, int32_t month, int32_t day) {
    return year * 10000 + month * 100 + day;
}

static bool is_leap(int32_t year) {
    if ((year & 3) != 0) return false;
    if (year % 100 != 0) return true;
    return year % 400 == 0;
}

static int32_t days_in_month(int32_t year, int32_t month) {
    static const uint8_t days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 30;
    if (month == 2 && is_leap(year)) return 29;
    return days[month - 1];
}

static int32_t date_serial(int32_t ymd) {
    civil_date_t date = split_ymd(ymd);
    int32_t total = 0;
    int32_t year;
    int32_t month;
    if (date.year >= 1970) {
        for (year = 1970; year < date.year; ++year) total += is_leap(year) ? 366 : 365;
    } else {
        for (year = 1969; year >= date.year; --year) total -= is_leap(year) ? 366 : 365;
    }
    for (month = 1; month < date.month; ++month) total += days_in_month(date.year, month);
    total += date.day - 1;
    return total;
}

static int32_t add_days(int32_t ymd, int32_t delta) {
    civil_date_t date = split_ymd(ymd);
    while (delta > 0) {
        ++date.day;
        if (date.day > days_in_month(date.year, date.month)) {
            date.day = 1;
            ++date.month;
            if (date.month > 12) {
                date.month = 1;
                ++date.year;
            }
        }
        --delta;
    }
    while (delta < 0) {
        --date.day;
        if (date.day < 1) {
            --date.month;
            if (date.month < 1) {
                date.month = 12;
                --date.year;
            }
            date.day = days_in_month(date.year, date.month);
        }
        ++delta;
    }
    return make_ymd(date.year, date.month, date.day);
}

static int32_t weekday(int32_t ymd) {
    int32_t value = (date_serial(ymd) + 4) % 7; /* 1970-01-01 was Thursday. */
    if (value < 0) value += 7;
    return value;
}

static int32_t today_ymd(void) {
    t5_local_datetime_t now;
    if (!g_system->local_datetime(&now)) return 19700101;
    return make_ymd(now.year, now.month, now.day);
}

static int32_t current_minutes(void) {
    t5_local_datetime_t now;
    if (!g_system->local_datetime(&now)) return 0;
    return (int32_t)now.hour * 60 + now.minute;
}

static int32_t sunday_ymd(int32_t week_offset) {
    int32_t today = today_ymd();
    return add_days(today, -weekday(today) + week_offset * 7);
}

static int32_t day_of_year(int32_t ymd) {
    civil_date_t date = split_ymd(ymd);
    int32_t value = date.day - 1;
    int32_t month;
    for (month = 1; month < date.month; ++month) value += days_in_month(date.year, month);
    return value;
}

static int32_t week_of_year(int32_t ymd) {
    return day_of_year(ymd) / 7 + 1;
}

static const char *month_abbrev(int32_t month) {
    static const char *names[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (month < 1 || month > 12) return "?";
    return names[month - 1];
}

static const char *weekday_abbrev(int32_t ymd) {
    static const char *names[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    return names[weekday(ymd)];
}

static const char *punch_label(uint8_t punch) {
    if (punch == PUNCH_CLOCK_IN) return "Clock in";
    if (punch == PUNCH_LUNCH_START) return "Lunch start";
    if (punch == PUNCH_LUNCH_END) return "Lunch end";
    return "Clock out";
}

static void format_ampm(int16_t minutes, char *out, size_t capacity) {
    int32_t hour24;
    int32_t minute;
    int32_t hour12;
    if (!out || capacity == 0) return;
    out[0] = '\0';
    if (minutes < 0) {
        append_text(out, capacity, "--");
        return;
    }
    hour24 = minutes / 60;
    minute = minutes % 60;
    hour12 = hour24 % 12;
    if (hour12 == 0) hour12 = 12;
    append_uint(out, capacity, (uint32_t)hour12);
    append_char(out, capacity, ':');
    append_two_digits(out, capacity, (uint32_t)minute);
    append_char(out, capacity, ' ');
    append_text(out, capacity, hour24 >= 12 ? "PM" : "AM");
}

static bool parse_time(const char *text, int16_t *minutes_out) {
    const char *p = text;
    int32_t hour = 0;
    int32_t minute = 0;
    bool have_hour = false;
    bool am = false;
    bool pm = false;
    if (!text || !minutes_out) return false;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == '\0') {
        *minutes_out = -1;
        return true;
    }
    while (*p >= '0' && *p <= '9') {
        have_hour = true;
        hour = hour * 10 + (*p - '0');
        ++p;
    }
    if (!have_hour) return false;
    if (*p == ':') {
        int digits = 0;
        ++p;
        while (*p >= '0' && *p <= '9' && digits < 2) {
            minute = minute * 10 + (*p - '0');
            ++p;
            ++digits;
        }
        if (digits == 0) return false;
    }
    while (*p) {
        if (*p == 'a' || *p == 'A') am = true;
        if (*p == 'p' || *p == 'P') pm = true;
        ++p;
    }
    if (minute < 0 || minute > 59) return false;
    if (am || pm) {
        if (hour < 1 || hour > 12) return false;
        hour %= 12;
        if (pm) hour += 12;
    } else if (hour > 23) {
        return false;
    }
    *minutes_out = (int16_t)(hour * 60 + minute);
    return true;
}

static int16_t worked_minutes(const timecard_day_t *day) {
    int16_t total;
    if (!day || day->punches[0] < 0 || day->punches[3] < day->punches[0]) return -1;
    total = (int16_t)(day->punches[3] - day->punches[0]);
    if (day->punches[1] >= 0 && day->punches[2] >= day->punches[1])
        total = (int16_t)(total - (day->punches[2] - day->punches[1]));
    return total < 0 ? 0 : total;
}

static timecard_day_t empty_day(int32_t ymd) {
    timecard_day_t day;
    day.ymd = ymd;
    for (int i = 0; i < PUNCH_COUNT; ++i) day.punches[i] = -1;
    return day;
}

static timecard_day_t *find_day(int32_t ymd) {
    for (int32_t i = 0; i < g_day_count; ++i)
        if (g_days[i].ymd == ymd) return &g_days[i];
    return NULL;
}

static timecard_day_t get_day(int32_t ymd) {
    timecard_day_t *found = find_day(ymd);
    return found ? *found : empty_day(ymd);
}

static timecard_day_t *ensure_day(int32_t ymd) {
    timecard_day_t *found = find_day(ymd);
    if (found) return found;
    if (g_day_count >= MAX_DAYS) {
        for (int32_t i = 1; i < g_day_count; ++i) g_days[i - 1] = g_days[i];
        --g_day_count;
    }
    g_days[g_day_count] = empty_day(ymd);
    return &g_days[g_day_count++];
}

static const char *find_text(const char *begin, const char *end, const char *needle) {
    size_t length = text_length(needle);
    if (!begin || !end || !needle || length == 0) return NULL;
    for (const char *p = begin; p + length <= end; ++p) {
        size_t i = 0;
        while (i < length && p[i] == needle[i]) ++i;
        if (i == length) return p;
    }
    return NULL;
}

static const char *matching_brace(const char *start, const char *end) {
    int32_t depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (const char *p = start; p < end; ++p) {
        char ch = *p;
        if (in_string) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') in_string = false;
            continue;
        }
        if (ch == '"') in_string = true;
        else if (ch == '{') ++depth;
        else if (ch == '}' && --depth == 0) return p;
    }
    return NULL;
}

static bool parse_int_field(const char *begin, const char *end, const char *key, int32_t *value) {
    const char *p = find_text(begin, end, key);
    bool negative = false;
    bool have_digit = false;
    int32_t out = 0;
    if (!p || !value) return false;
    p += text_length(key);
    while (p < end && *p != ':') ++p;
    if (p == end) return false;
    ++p;
    while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p;
    if (p < end && *p == '-') {
        negative = true;
        ++p;
    }
    while (p < end && *p >= '0' && *p <= '9') {
        out = out * 10 + (*p - '0');
        have_digit = true;
        ++p;
    }
    if (!have_digit) return false;
    *value = negative ? -out : out;
    return true;
}

static bool load_store(void) {
    size_t size = 0;
    t5_storage_result_t result;
    g_day_count = 0;
    result = g_storage->read_file(STORE_PATH, g_json, sizeof(g_json) - 1, &size);
    if (result == T5_STORAGE_NOT_FOUND) return true;
    if (result != T5_STORAGE_OK || size >= sizeof(g_json)) return false;
    g_json[size] = '\0';

    const char *end = g_json + size;
    const char *days = find_text(g_json, end, "\"days\"");
    if (!days) return false;
    const char *p = days;
    while (p < end && *p != '[') ++p;
    if (p == end) return false;
    ++p;

    while (p < end && *p != ']' && g_day_count < MAX_DAYS) {
        while (p < end && *p != '{' && *p != ']') ++p;
        if (p >= end || *p == ']') break;
        const char *object_end = matching_brace(p, end);
        int32_t ymd = 0;
        if (!object_end || !parse_int_field(p, object_end, "\"d\"", &ymd)) return false;
        if (ymd >= 19700101) {
            timecard_day_t day = empty_day(ymd);
            int32_t value;
            if (parse_int_field(p, object_end, "\"in\"", &value)) day.punches[0] = (int16_t)value;
            if (parse_int_field(p, object_end, "\"ls\"", &value)) day.punches[1] = (int16_t)value;
            if (parse_int_field(p, object_end, "\"le\"", &value)) day.punches[2] = (int16_t)value;
            if (parse_int_field(p, object_end, "\"out\"", &value)) day.punches[3] = (int16_t)value;
            g_days[g_day_count++] = day;
        }
        p = object_end + 1;
    }
    return true;
}

static bool json_char(size_t *used, char ch) {
    if (!used || *used + 1 >= sizeof(g_json)) return false;
    g_json[(*used)++] = ch;
    g_json[*used] = '\0';
    return true;
}

static bool json_text(size_t *used, const char *text) {
    if (!text) return false;
    while (*text) if (!json_char(used, *text++)) return false;
    return true;
}

static bool json_int(size_t *used, int32_t value) {
    char reverse[16];
    size_t count = 0;
    uint32_t positive;
    if (value < 0) {
        if (!json_char(used, '-')) return false;
        positive = (uint32_t)(-value);
    } else {
        positive = (uint32_t)value;
    }
    if (positive == 0) return json_char(used, '0');
    while (positive && count < sizeof(reverse)) {
        uint32_t quotient = positive / 10u;
        reverse[count++] = (char)('0' + positive - quotient * 10u);
        positive = quotient;
    }
    while (count) if (!json_char(used, reverse[--count])) return false;
    return true;
}

static bool day_has_any(const timecard_day_t *day) {
    if (!day) return false;
    for (int i = 0; i < PUNCH_COUNT; ++i) if (day->punches[i] >= 0) return true;
    return false;
}

static bool save_store(void) {
    size_t used = 0;
    if (!json_text(&used, "{\"days\":[")) return false;
    bool first = true;
    for (int32_t i = 0; i < g_day_count; ++i) {
        timecard_day_t *day = &g_days[i];
        if (!day_has_any(day)) continue;
        if (!first && !json_char(&used, ',')) return false;
        first = false;
        if (!json_text(&used, "{\"d\":")) return false;
        if (!json_int(&used, day->ymd)) return false;
        if (day->punches[0] >= 0) {
            if (!json_text(&used, ",\"in\":")) return false;
            if (!json_int(&used, day->punches[0])) return false;
        }
        if (day->punches[1] >= 0) {
            if (!json_text(&used, ",\"ls\":")) return false;
            if (!json_int(&used, day->punches[1])) return false;
        }
        if (day->punches[2] >= 0) {
            if (!json_text(&used, ",\"le\":")) return false;
            if (!json_int(&used, day->punches[2])) return false;
        }
        if (day->punches[3] >= 0) {
            if (!json_text(&used, ",\"out\":")) return false;
            if (!json_int(&used, day->punches[3])) return false;
        }
        if (!json_char(&used, '}')) return false;
    }
    if (!json_text(&used, "]}")) return false;
    return g_storage->write_file_atomic(STORE_PATH, g_json, used);
}

static bool set_punch(int32_t ymd, uint8_t punch, int16_t minutes) {
    timecard_day_t *day;
    if (ymd < 19700101 || punch >= PUNCH_COUNT) return false;
    if (minutes > 1439) minutes = 1439;
    if (minutes < 0) minutes = -1;
    day = ensure_day(ymd);
    if (!day) return false;
    day->punches[punch] = minutes;
    return save_store();
}

static void format_week_label(int32_t sunday, char *out, size_t capacity) {
    int32_t saturday = add_days(sunday, 6);
    civil_date_t start = split_ymd(sunday);
    civil_date_t end = split_ymd(saturday);
    if (!out || capacity == 0) return;
    out[0] = '\0';
    append_text(out, capacity, "Week ");
    append_uint(out, capacity, (uint32_t)week_of_year(sunday));
    append_text(out, capacity, "  ");
    append_text(out, capacity, month_abbrev(start.month));
    append_char(out, capacity, ' ');
    append_uint(out, capacity, (uint32_t)start.day);
    append_text(out, capacity, " - ");
    if (start.month != end.month) {
        append_text(out, capacity, month_abbrev(end.month));
        append_char(out, capacity, ' ');
    }
    append_uint(out, capacity, (uint32_t)end.day);
}

static void set_punch_status(uint8_t punch, int16_t minutes) {
    char time[24];
    g_status[0] = '\0';
    append_text(g_status, sizeof(g_status), punch_label(punch));
    append_text(g_status, sizeof(g_status), "  ");
    format_ampm(minutes, time, sizeof(time));
    append_text(g_status, sizeof(g_status), time);
}

static int32_t item_count(void) {
    if (g_screen == SCREEN_WEEK_LIST) return WEEK_HISTORY;
    if (g_screen == SCREEN_DAY) return PUNCH_COUNT;
    return WEEK_ITEM_COUNT;
}

static int32_t selected_for_today(void) {
    int32_t today = today_ymd();
    int32_t sunday = sunday_ymd(0);
    for (int32_t i = 0; i < DAY_COUNT; ++i) if (add_days(sunday, i) == today) return i;
    return 0;
}

static void move_selection(int32_t delta) {
    int32_t count = item_count();
    if (count <= 0) return;
    g_selected += delta;
    if (g_selected < 0) g_selected = count - 1;
    else if (g_selected >= count) g_selected = 0;
}

static int32_t list_top(void) {
    return g_screen == SCREEN_WEEK ? 142 : 112;
}

static int32_t row_height(void) {
    if (g_screen == SCREEN_WEEK_LIST) return 36;
    if (g_screen == SCREEN_DAY) return 92;
    return 54;
}

static void draw_footer(void) {
    int32_t height = g_app->screen_height();
    g_app->draw_text(20, height - 56, g_status);
    if (g_screen == SCREEN_WEEK_LIST)
        g_app->draw_text(20, height - 28, "Back: Home   Enter: Open   Up/Down: Select");
    else
        g_app->draw_text(20, height - 28, "Back   Enter: Select   Up/Down");
}

static void render_week_list(void) {
    char label[64];
    g_app->draw_text(20, 20, "Time Card");
    g_app->draw_text(20, 58, "Weeks");
    for (int32_t i = 0; i < WEEK_HISTORY; ++i) {
        char line[96] = {0};
        append_text(line, sizeof(line), i == g_selected ? "> " : "  ");
        format_week_label(sunday_ymd(-i), label, sizeof(label));
        append_text(line, sizeof(line), label);
        if (i == 0) append_text(line, sizeof(line), "  This week");
        g_app->draw_text(20, list_top() + i * row_height(), line);
    }
}

static void draw_week_columns(void) {
    g_app->draw_text(20, 100, "Day");
    g_app->draw_text(92, 100, "In");
    g_app->draw_text(184, 100, "Start");
    g_app->draw_text(286, 100, "End");
    g_app->draw_text(382, 100, "Out");
}

static void render_week(void) {
    char title[64];
    int32_t sunday = sunday_ymd(g_week_offset);
    int32_t today = today_ymd();
    g_app->draw_text(20, 20, "Time Card");
    format_week_label(sunday, title, sizeof(title));
    g_app->draw_text(20, 58, title);
    draw_week_columns();

    for (int32_t i = 0; i < DAY_COUNT; ++i) {
        int32_t ymd = add_days(sunday, i);
        civil_date_t date = split_ymd(ymd);
        timecard_day_t day = get_day(ymd);
        char day_label[24] = {0};
        char value[24];
        int32_t y = list_top() + i * row_height();
        append_text(day_label, sizeof(day_label), i == g_selected ? ">" : " ");
        append_text(day_label, sizeof(day_label), weekday_abbrev(ymd));
        append_char(day_label, sizeof(day_label), ' ');
        append_uint(day_label, sizeof(day_label), (uint32_t)date.day);
        if (ymd == today) append_char(day_label, sizeof(day_label), '*');
        g_app->draw_text(20, y, day_label);
        format_ampm(day.punches[0], value, sizeof(value)); g_app->draw_text(92, y, value);
        format_ampm(day.punches[1], value, sizeof(value)); g_app->draw_text(184, y, value);
        format_ampm(day.punches[2], value, sizeof(value)); g_app->draw_text(286, y, value);
        format_ampm(day.punches[3], value, sizeof(value)); g_app->draw_text(382, y, value);
    }

    for (int32_t i = 0; i < PUNCH_COUNT; ++i) {
        char line[64] = {0};
        int32_t index = DAY_COUNT + i;
        int32_t y = list_top() + index * row_height();
        append_text(line, sizeof(line), index == g_selected ? "> " : "  ");
        append_text(line, sizeof(line), punch_label((uint8_t)i));
        g_app->draw_text(20, y, line);
    }
}

static void render_day(void) {
    civil_date_t date = split_ymd(g_editing_ymd);
    timecard_day_t day = get_day(g_editing_ymd);
    char subtitle[48] = {0};
    char value[24];
    char total[24];
    g_app->draw_text(20, 20, "Time Card");
    append_text(subtitle, sizeof(subtitle), weekday_abbrev(g_editing_ymd));
    append_char(subtitle, sizeof(subtitle), ' ');
    append_text(subtitle, sizeof(subtitle), month_abbrev(date.month));
    append_char(subtitle, sizeof(subtitle), ' ');
    append_uint(subtitle, sizeof(subtitle), (uint32_t)date.day);
    g_app->draw_text(20, 58, subtitle);

    for (int32_t i = 0; i < PUNCH_COUNT; ++i) {
        char line[96] = {0};
        append_text(line, sizeof(line), i == g_selected ? "> " : "  ");
        append_text(line, sizeof(line), punch_label((uint8_t)i));
        format_ampm(day.punches[i], value, sizeof(value));
        append_text(line, sizeof(line), "    ");
        append_text(line, sizeof(line), value);
        g_app->draw_text(20, list_top() + i * row_height(), line);
    }
    int16_t worked = worked_minutes(&day);
    if (worked >= 0) {
        total[0] = '\0';
        append_text(total, sizeof(total), "Worked: ");
        append_uint(total, sizeof(total), (uint32_t)(worked / 60));
        append_char(total, sizeof(total), ':');
        append_two_digits(total, sizeof(total), (uint32_t)(worked % 60));
        g_app->draw_text(20, list_top() + PUNCH_COUNT * row_height() + 22, total);
    }
}

static void render(void) {
    g_app->clear();
    if (g_screen == SCREEN_WEEK_LIST) render_week_list();
    else if (g_screen == SCREEN_WEEK) render_week();
    else render_day();
    draw_footer();
    g_app->present(true);
}

static uint8_t touch(int16_t y, int32_t *selected) {
    int32_t top = list_top();
    int32_t height = row_height();
    int32_t count = item_count();
    if (!selected) return TOUCH_NONE;
    if (y < 88) return g_screen == SCREEN_WEEK_LIST ? TOUCH_NONE : TOUCH_HEADER;
    if (g_screen == SCREEN_WEEK && y < top) return TOUCH_HEADER;
    if (y < top) return TOUCH_NONE;
    int32_t offset = y - top;
    int32_t index = 0;
    while (offset >= height) {
        offset -= height;
        ++index;
    }
    if (index < 0 || index >= count) return TOUCH_NONE;
    *selected = index;
    return TOUCH_ITEM;
}

static void open_week(int32_t offset) {
    g_week_offset = offset;
    g_screen = SCREEN_WEEK;
    g_selected = offset == 0 ? selected_for_today() : 0;
    g_editing_ymd = 0;
    set_status("Open a day, or punch below");
}

static void open_day(int32_t ymd) {
    g_editing_ymd = ymd;
    g_screen = SCREEN_DAY;
    g_selected = 0;
    set_status(day_has_any(&get_day(ymd)) ? "Confirm a row to edit" : "Confirm a row to set time");
}

static void punch_today(uint8_t punch) {
    int32_t today = today_ymd();
    int16_t minutes = (int16_t)current_minutes();
    if (!set_punch(today, punch, minutes)) {
        set_status("Could not save punch");
        return;
    }
    g_week_offset = 0;
    g_screen = SCREEN_WEEK;
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
    timecard_day_t day;
    char initial[24];
    uint8_t punch = (uint8_t)g_selected;
    if (g_screen != SCREEN_DAY || punch >= PUNCH_COUNT) return false;
    day = get_day(g_editing_ymd);
    format_ampm(day.punches[punch], initial, sizeof(initial));
    if (day.punches[punch] < 0) initial[0] = '\0';
    if (!g_ui->keyboard_request(punch_label(punch), initial, 12, T5_SYSTEM_KEYBOARD_TEXT,
                                make_edit_cookie(g_editing_ymd, punch, g_week_offset))) {
        set_status("Keyboard unavailable");
        return false;
    }
    return true;
}

static bool activate(void) {
    if (g_screen == SCREEN_WEEK_LIST) {
        open_week(-g_selected);
        return false;
    }
    if (g_screen == SCREEN_DAY) return request_edit();
    if (g_selected < DAY_COUNT) open_day(add_days(sunday_ymd(g_week_offset), g_selected));
    else punch_today((uint8_t)(g_selected - DAY_COUNT));
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
    if (!g_ui->keyboard_take_result(text, sizeof(text), &cancelled, &cookie)) return false;
    if (!decode_edit_cookie(cookie, &ymd, &punch, &week_offset) || punch >= PUNCH_COUNT || ymd < 19700101) {
        set_status("Keyboard result ignored");
        return true;
    }

    load_store();
    g_screen = SCREEN_DAY;
    g_week_offset = week_offset;
    g_selected = punch;
    g_editing_ymd = ymd;
    if (cancelled) {
        timecard_day_t day = get_day(ymd);
        set_status(day_has_any(&day) ? "Confirm a row to edit" : "Confirm a row to set time");
        return true;
    }
    if (!parse_time(text, &minutes) || !set_punch(ymd, punch, minutes)) {
        set_status("Edit time");
        return true;
    }
    set_punch_status(punch, minutes);
    return true;
}

static bool has_required_apis(void) {
    size_t app_required = offsetof(t5_app_api_v1, set_back_exits_app) + sizeof(g_app->set_back_exits_app);
    size_t storage_required = offsetof(t5_storage_api_v1, write_file_atomic) + sizeof(g_storage->write_file_atomic);
    size_t system_required = offsetof(t5_system_api_v1, local_datetime) + sizeof(g_system->local_datetime);
    size_t ui_required = offsetof(t5_system_ui_api_v1, navigate_home) + sizeof(g_ui->navigate_home);
    return g_app && g_app->abi_version == T5_APP_ABI_VERSION && g_app->struct_size >= app_required &&
           g_app->poll && g_app->set_back_exits_app &&
           g_storage && g_storage->abi_version == T5_STORAGE_ABI_VERSION &&
           g_storage->struct_size >= storage_required && g_storage->read_file && g_storage->write_file_atomic &&
           g_system && g_system->abi_version == T5_SYSTEM_ABI_VERSION &&
           g_system->struct_size >= system_required && g_system->local_datetime &&
           g_ui && g_ui->api_version == T5_SYSTEM_UI_API_VERSION && g_ui->struct_size >= ui_required &&
           g_ui->keyboard_request && g_ui->keyboard_take_result && g_ui->navigate_home;
}

__attribute__((visibility("default"))) void app_main(void) {
    t5_app_input_t input;
    bool armed = false;

    g_app = t5_app_get_api(T5_APP_ABI_VERSION);
    g_storage = t5_storage_get_api(T5_STORAGE_ABI_VERSION);
    g_system = t5_system_get_api(T5_SYSTEM_ABI_VERSION);
    g_ui = t5_system_ui_get_api(T5_SYSTEM_UI_API_VERSION);
    if (!has_required_apis()) return;

    g_app->set_back_exits_app(false);
    load_store();
    if (!consume_keyboard_result()) {
        g_screen = SCREEN_WEEK_LIST;
        g_week_offset = 0;
        g_selected = 0;
        g_editing_ymd = 0;
        set_status("Select a week");
    }
    render();

    while (g_app->poll(&input, 20)) {
        uint32_t buttons = input.buttons;
        if (input.exit_requested) return;
        if (buttons == 0 && !input.tapped) {
            armed = true;
            continue;
        }
        if (!armed) continue;
        armed = false;

        if (input.tapped) {
            int32_t touched = g_selected;
            uint8_t hit = touch(input.touch_y, &touched);
            if (hit == TOUCH_HEADER) {
                g_screen = SCREEN_WEEK_LIST;
                g_selected = -g_week_offset;
                if (g_selected < 0) g_selected = 0;
                if (g_selected >= WEEK_HISTORY) g_selected = WEEK_HISTORY - 1;
                g_editing_ymd = 0;
                set_status("Select a week");
                render();
                continue;
            }
            if (hit == TOUCH_ITEM) {
                g_selected = touched;
                if (activate()) return;
                render();
            }
            continue;
        }

        if (buttons & T5_APP_BUTTON_BACK) {
            if (g_screen == SCREEN_DAY) {
                g_screen = SCREEN_WEEK;
                g_editing_ymd = 0;
                g_selected = 0;
                set_status("Open a day, or punch below");
                render();
                continue;
            }
            if (g_screen == SCREEN_WEEK) {
                g_screen = SCREEN_WEEK_LIST;
                g_selected = -g_week_offset;
                if (g_selected < 0) g_selected = 0;
                if (g_selected >= WEEK_HISTORY) g_selected = WEEK_HISTORY - 1;
                set_status("Select a week");
                render();
                continue;
            }
            g_app->set_back_exits_app(true);
            g_ui->navigate_home();
            return;
        }
        if (buttons & T5_APP_BUTTON_CONFIRM) {
            if (activate()) {
                g_app->set_back_exits_app(true);
                return;
            }
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
    g_app->set_back_exits_app(true);
}
