#include "T5AppApi.h"
#include "T5StorageApi.h"
#include "T5SystemApi.h"
#include "T5SystemUiApi.h"
#include "T5UiApi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DAY_COUNT 7
#define PUNCH_COUNT 4
#define WEEK_HISTORY 20
#define WEEK_ITEM_COUNT 11
#define MAX_DAYS 400
#define JSON_CAPACITY 49152
#define STATUS_CAP 128
#define STORE_PATH "/sd/.crosspoint/timecard.json"
#define COOKIE_MAGIC 0x54u

#define SCREEN_WEEK_LIST 0u
#define SCREEN_WEEK 1u
#define SCREEN_DAY 2u

typedef struct {
    int32_t ymd;
    int16_t punches[PUNCH_COUNT];
} tc_day_t;

typedef struct {
    int32_t year;
    int32_t month;
    int32_t day;
} civil_t;

static const t5_app_api_v1 *app;
static const t5_storage_api_v1 *storage;
static const t5_system_api_v1 *system_api;
static const t5_system_ui_api_v1 *system_ui;
static const t5_ui_api_v1 *fwui;

static tc_day_t days[MAX_DAYS];
static int32_t day_count;
static char json[JSON_CAPACITY];
static uint8_t screen_id;
static int32_t week_offset;
static int32_t selected;
static int32_t editing_ymd;
static char status_text[STATUS_CAP];

// Reusable UI buffers. The firmware renderer consumes these synchronously.
static t5_ui_list_row_t list_rows[WEEK_HISTORY];
static char list_titles[WEEK_HISTORY][80];
static char list_subtitles[WEEK_HISTORY][24];
static char list_values[WEEK_HISTORY][32];
static t5_ui_table_row_t table_rows[WEEK_ITEM_COUNT];
static char table_cells[WEEK_ITEM_COUNT][T5_UI_MAX_COLUMNS][32];

static size_t slen(const char *s) {
    size_t n = 0;
    if (s) while (s[n]) ++n;
    return n;
}

static void scopy(char *dst, size_t capacity, const char *src) {
    size_t i = 0;
    if (!dst || !capacity) return;
    if (!src) src = "";
    while (src[i] && i + 1 < capacity) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static void sadd(char *dst, size_t capacity, const char *src) {
    size_t n = slen(dst), i = 0;
    if (!dst || !src || n >= capacity) return;
    while (src[i] && n + 1 < capacity) dst[n++] = src[i++];
    dst[n] = 0;
}

static void schar(char *dst, size_t capacity, char ch) {
    size_t n = slen(dst);
    if (!dst || n + 1 >= capacity) return;
    dst[n] = ch;
    dst[n + 1] = 0;
}

static void suint(char *dst, size_t capacity, uint32_t value) {
    char reversed[12];
    size_t n = 0;
    if (!value) {
        schar(dst, capacity, '0');
        return;
    }
    while (value && n < sizeof(reversed)) {
        uint32_t q = value / 10u;
        reversed[n++] = (char)('0' + value - q * 10u);
        value = q;
    }
    while (n) schar(dst, capacity, reversed[--n]);
}

static void s2(char *dst, size_t capacity, uint32_t value) {
    schar(dst, capacity, (char)('0' + (value / 10u) % 10u));
    schar(dst, capacity, (char)('0' + value % 10u));
}

static void set_status(const char *text) { scopy(status_text, sizeof(status_text), text); }

static civil_t split_ymd(int32_t value) {
    civil_t result;
    result.year = value / 10000;
    value -= result.year * 10000;
    result.month = value / 100;
    result.day = value - result.month * 100;
    return result;
}

static int32_t make_ymd(int32_t year, int32_t month, int32_t day) { return year * 10000 + month * 100 + day; }

static bool leap(int32_t year) {
    if ((year & 3) != 0) return false;
    if (year % 100 != 0) return true;
    return year % 400 == 0;
}

static int32_t month_days(int32_t year, int32_t month) {
    static const uint8_t count[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 30;
    if (month == 2 && leap(year)) return 29;
    return count[month - 1];
}

static int32_t serial_day(int32_t value) {
    civil_t date = split_ymd(value);
    int32_t result = 0;
    int32_t year, month;
    if (date.year >= 1970) {
        for (year = 1970; year < date.year; ++year) result += leap(year) ? 366 : 365;
    } else {
        for (year = 1969; year >= date.year; --year) result -= leap(year) ? 366 : 365;
    }
    for (month = 1; month < date.month; ++month) result += month_days(date.year, month);
    return result + date.day - 1;
}

static int32_t add_days(int32_t value, int32_t delta) {
    civil_t date = split_ymd(value);
    while (delta > 0) {
        ++date.day;
        if (date.day > month_days(date.year, date.month)) {
            date.day = 1;
            if (++date.month > 12) {
                date.month = 1;
                ++date.year;
            }
        }
        --delta;
    }
    while (delta < 0) {
        if (--date.day < 1) {
            if (--date.month < 1) {
                date.month = 12;
                --date.year;
            }
            date.day = month_days(date.year, date.month);
        }
        ++delta;
    }
    return make_ymd(date.year, date.month, date.day);
}

static int32_t weekday(int32_t value) {
    int32_t day = (serial_day(value) + 4) % 7;
    if (day < 0) day += 7;
    return day;
}

static int32_t today(void) {
    t5_local_datetime_t now;
    if (!system_api->local_datetime(&now)) return 19700101;
    return make_ymd(now.year, now.month, now.day);
}

static int32_t now_minutes(void) {
    t5_local_datetime_t now;
    if (!system_api->local_datetime(&now)) return 0;
    return (int32_t)now.hour * 60 + now.minute;
}

static int32_t sunday(int32_t offset) {
    int32_t value = today();
    return add_days(value, -weekday(value) + offset * 7);
}

static int32_t week_number(int32_t value) {
    civil_t date = split_ymd(value);
    int32_t day = date.day - 1;
    for (int32_t month = 1; month < date.month; ++month) day += month_days(date.year, month);
    return day / 7 + 1;
}

static const char *month_name(int32_t month) {
    static const char *names[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    return month >= 1 && month <= 12 ? names[month - 1] : "?";
}

static const char *day_name(int32_t value) {
    static const char *names[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    return names[weekday(value)];
}

static const char *punch_name(uint8_t punch) {
    static const char *names[PUNCH_COUNT] = {"Clock in", "Lunch start", "Lunch end", "Clock out"};
    return punch < PUNCH_COUNT ? names[punch] : "Time";
}

static tc_day_t blank_day(int32_t value) {
    tc_day_t day;
    day.ymd = value;
    for (int i = 0; i < PUNCH_COUNT; ++i) day.punches[i] = -1;
    return day;
}

static tc_day_t *find_day(int32_t value) {
    for (int32_t i = 0; i < day_count; ++i) {
        if (days[i].ymd == value) return &days[i];
    }
    return NULL;
}

static tc_day_t get_day(int32_t value) {
    tc_day_t *day = find_day(value);
    return day ? *day : blank_day(value);
}

static tc_day_t *ensure_day(int32_t value) {
    tc_day_t *day = find_day(value);
    if (day) return day;
    if (day_count >= MAX_DAYS) {
        for (int32_t i = 1; i < day_count; ++i) days[i - 1] = days[i];
        --day_count;
    }
    days[day_count] = blank_day(value);
    return &days[day_count++];
}

static bool any_punch(const tc_day_t *day) {
    if (!day) return false;
    for (int i = 0; i < PUNCH_COUNT; ++i) {
        if (day->punches[i] >= 0) return true;
    }
    return false;
}

static const char *find_text(const char *begin, const char *end, const char *needle) {
    size_t length = slen(needle);
    if (!begin || !end || !needle || !length) return NULL;
    for (const char *p = begin; p + length <= end; ++p) {
        size_t i = 0;
        while (i < length && p[i] == needle[i]) ++i;
        if (i == length) return p;
    }
    return NULL;
}

static const char *close_brace(const char *start, const char *end) {
    int depth = 0;
    bool in_string = false, escaped = false;
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

static bool int_field(const char *begin, const char *end, const char *key, int32_t *out) {
    const char *p = find_text(begin, end, key);
    int32_t value = 0;
    bool negative = false, any = false;
    if (!p || !out) return false;
    p += slen(key);
    while (p < end && *p != ':') ++p;
    if (p == end) return false;
    ++p;
    while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p;
    if (p < end && *p == '-') {
        negative = true;
        ++p;
    }
    while (p < end && *p >= '0' && *p <= '9') {
        value = value * 10 + (*p - '0');
        any = true;
        ++p;
    }
    if (!any) return false;
    *out = negative ? -value : value;
    return true;
}

static bool load_store(void) {
    size_t size = 0;
    day_count = 0;
    if (!storage->exists(STORE_PATH)) return true;
    if (!storage->read_file(STORE_PATH, json, sizeof(json) - 1, &size) || size >= sizeof(json)) return false;
    json[size] = 0;

    const char *end = json + size;
    const char *p = find_text(json, end, "\"days\"");
    if (!p) return false;
    while (p < end && *p != '[') ++p;
    if (p == end) return false;
    ++p;

    while (p < end && *p != ']' && day_count < MAX_DAYS) {
        while (p < end && *p != '{' && *p != ']') ++p;
        if (p >= end || *p == ']') break;
        const char *close = close_brace(p, end);
        int32_t date = 0;
        if (!close || !int_field(p, close, "\"d\"", &date)) return false;
        if (date >= 19700101) {
            tc_day_t day = blank_day(date);
            int32_t value;
            if (int_field(p, close, "\"in\"", &value)) day.punches[0] = (int16_t)value;
            if (int_field(p, close, "\"ls\"", &value)) day.punches[1] = (int16_t)value;
            if (int_field(p, close, "\"le\"", &value)) day.punches[2] = (int16_t)value;
            if (int_field(p, close, "\"out\"", &value)) day.punches[3] = (int16_t)value;
            days[day_count++] = day;
        }
        p = close + 1;
    }
    return true;
}

static bool json_char(size_t *used, char ch) {
    if (!used || *used + 1 >= sizeof(json)) return false;
    json[(*used)++] = ch;
    json[*used] = 0;
    return true;
}

static bool json_text(size_t *used, const char *text) {
    if (!text) return false;
    while (*text) if (!json_char(used, *text++)) return false;
    return true;
}

static bool json_int(size_t *used, int32_t value) {
    char reversed[16];
    size_t n = 0;
    uint32_t positive;
    if (value < 0) {
        if (!json_char(used, '-')) return false;
        positive = (uint32_t)(-value);
    } else {
        positive = (uint32_t)value;
    }
    if (!positive) return json_char(used, '0');
    while (positive && n < sizeof(reversed)) {
        uint32_t q = positive / 10u;
        reversed[n++] = (char)('0' + positive - q * 10u);
        positive = q;
    }
    while (n) if (!json_char(used, reversed[--n])) return false;
    return true;
}

static bool save_store(void) {
    size_t used = 0;
    bool first = true;
    if (!json_text(&used, "{\"days\":[")) return false;
    for (int32_t i = 0; i < day_count; ++i) {
        tc_day_t *day = &days[i];
        if (!any_punch(day)) continue;
        if (!first && !json_char(&used, ',')) return false;
        first = false;
        if (!json_text(&used, "{\"d\":") || !json_int(&used, day->ymd)) return false;
        static const char *keys[PUNCH_COUNT] = {",\"in\":", ",\"ls\":", ",\"le\":", ",\"out\":"};
        for (int punch = 0; punch < PUNCH_COUNT; ++punch) {
            if (day->punches[punch] >= 0) {
                if (!json_text(&used, keys[punch]) || !json_int(&used, day->punches[punch])) return false;
            }
        }
        if (!json_char(&used, '}')) return false;
    }
    if (!json_text(&used, "]}")) return false;
    return storage->write_file_atomic(STORE_PATH, json, used);
}

static bool set_punch(int32_t date, uint8_t punch, int16_t minutes) {
    if (date < 19700101 || punch >= PUNCH_COUNT) return false;
    if (minutes > 1439) minutes = 1439;
    if (minutes < 0) minutes = -1;
    tc_day_t *day = ensure_day(date);
    if (!day) return false;
    day->punches[punch] = minutes;
    return save_store();
}

static void format_ampm(int16_t minutes, char *out, size_t capacity) {
    if (!out || !capacity) return;
    out[0] = 0;
    if (minutes < 0) {
        sadd(out, capacity, "--");
        return;
    }
    int hour24 = minutes / 60;
    int minute = minutes % 60;
    int hour = hour24 % 12;
    if (!hour) hour = 12;
    suint(out, capacity, (uint32_t)hour);
    schar(out, capacity, ':');
    s2(out, capacity, (uint32_t)minute);
    schar(out, capacity, ' ');
    sadd(out, capacity, hour24 >= 12 ? "PM" : "AM");
}

static bool parse_time(const char *text, int16_t *out) {
    const char *p = text;
    int hour = 0, minute = 0, digits = 0;
    bool am = false, pm = false;
    if (!text || !out) return false;
    while (*p == ' ' || *p == '\t') ++p;
    if (!*p) {
        *out = -1;
        return true;
    }
    while (*p >= '0' && *p <= '9') {
        hour = hour * 10 + (*p - '0');
        ++p;
        ++digits;
    }
    if (!digits) return false;
    if (*p == ':') {
        digits = 0;
        ++p;
        while (*p >= '0' && *p <= '9' && digits < 2) {
            minute = minute * 10 + (*p - '0');
            ++p;
            ++digits;
        }
        if (!digits) return false;
    }
    while (*p) {
        if (*p == 'a' || *p == 'A') am = true;
        if (*p == 'p' || *p == 'P') pm = true;
        ++p;
    }
    if (minute > 59) return false;
    if (am || pm) {
        if (hour < 1 || hour > 12) return false;
        hour %= 12;
        if (pm) hour += 12;
    } else if (hour > 23) {
        return false;
    }
    *out = (int16_t)(hour * 60 + minute);
    return true;
}

static int16_t worked(const tc_day_t *day) {
    if (!day || day->punches[0] < 0 || day->punches[3] < day->punches[0]) return -1;
    int16_t total = (int16_t)(day->punches[3] - day->punches[0]);
    if (day->punches[1] >= 0 && day->punches[2] >= day->punches[1]) {
        total = (int16_t)(total - (day->punches[2] - day->punches[1]));
    }
    return total < 0 ? 0 : total;
}

static void week_label(int32_t sun, char *out, size_t capacity) {
    civil_t begin = split_ymd(sun);
    civil_t end = split_ymd(add_days(sun, 6));
    out[0] = 0;
    sadd(out, capacity, "Week ");
    suint(out, capacity, (uint32_t)week_number(sun));
    sadd(out, capacity, "  ");
    sadd(out, capacity, month_name(begin.month));
    schar(out, capacity, ' ');
    suint(out, capacity, (uint32_t)begin.day);
    sadd(out, capacity, " - ");
    if (begin.month != end.month) {
        sadd(out, capacity, month_name(end.month));
        schar(out, capacity, ' ');
    }
    suint(out, capacity, (uint32_t)end.day);
}

static void punch_status(uint8_t punch, int16_t minutes) {
    char time[24];
    status_text[0] = 0;
    sadd(status_text, sizeof(status_text), punch_name(punch));
    sadd(status_text, sizeof(status_text), "  ");
    format_ampm(minutes, time, sizeof(time));
    sadd(status_text, sizeof(status_text), time);
}

static int32_t item_count(void) {
    if (screen_id == SCREEN_WEEK_LIST) return WEEK_HISTORY;
    if (screen_id == SCREEN_DAY) return PUNCH_COUNT;
    return WEEK_ITEM_COUNT;
}

static int32_t selected_today(void) {
    int32_t current = today();
    int32_t start = sunday(0);
    for (int i = 0; i < DAY_COUNT; ++i) {
        if (add_days(start, i) == current) return i;
    }
    return 0;
}

static void clear_list_row(int index) {
    list_titles[index][0] = 0;
    list_subtitles[index][0] = 0;
    list_values[index][0] = 0;
    list_rows[index].title = list_titles[index];
    list_rows[index].subtitle = list_subtitles[index];
    list_rows[index].value = list_values[index];
    list_rows[index].flags = 0;
}

static void clear_table_row(int index) {
    table_rows[index].flags = 0;
    for (int column = 0; column < (int)T5_UI_MAX_COLUMNS; ++column) {
        table_cells[index][column][0] = 0;
        table_rows[index].cells[column] = table_cells[index][column];
    }
}

static t5_ui_chrome_t chrome(const char *subtitle, const char *confirm) {
    t5_ui_chrome_t value;
    value.title = "Time Card";
    value.subtitle = subtitle;
    value.status = status_text;
    value.back_label = screen_id == SCREEN_WEEK_LIST ? "Home" : "Back";
    value.confirm_label = confirm;
    value.previous_label = "Up";
    value.next_label = "Down";
    return value;
}

static void render_week_list(void) {
    for (int i = 0; i < WEEK_HISTORY; ++i) {
        clear_list_row(i);
        week_label(sunday(-i), list_titles[i], sizeof(list_titles[i]));
        if (i == 0) scopy(list_subtitles[i], sizeof(list_subtitles[i]), "This week");
    }
    t5_ui_chrome_t page = chrome("Weeks", "Open");
    fwui->render_list(&page, list_rows, WEEK_HISTORY, selected);
}

static void render_day(void) {
    civil_t date = split_ymd(editing_ymd);
    tc_day_t day = get_day(editing_ymd);
    char subtitle[48] = {0};
    sadd(subtitle, sizeof(subtitle), day_name(editing_ymd));
    schar(subtitle, sizeof(subtitle), ' ');
    sadd(subtitle, sizeof(subtitle), month_name(date.month));
    schar(subtitle, sizeof(subtitle), ' ');
    suint(subtitle, sizeof(subtitle), (uint32_t)date.day);

    for (int punch = 0; punch < PUNCH_COUNT; ++punch) {
        clear_list_row(punch);
        scopy(list_titles[punch], sizeof(list_titles[punch]), punch_name((uint8_t)punch));
        format_ampm(day.punches[punch], list_values[punch], sizeof(list_values[punch]));
        list_rows[punch].flags = T5_UI_LIST_HIGHLIGHT_VALUE;
    }

    int16_t minutes = worked(&day);
    char day_status[STATUS_CAP];
    scopy(day_status, sizeof(day_status), status_text);
    if (minutes >= 0) {
        sadd(day_status, sizeof(day_status), "   Worked ");
        suint(day_status, sizeof(day_status), (uint32_t)(minutes / 60));
        schar(day_status, sizeof(day_status), ':');
        s2(day_status, sizeof(day_status), (uint32_t)(minutes % 60));
    }

    t5_ui_chrome_t page = chrome(subtitle, "Select");
    page.status = day_status;
    fwui->render_list(&page, list_rows, PUNCH_COUNT, selected);
}

static void render_week(void) {
    static const t5_ui_table_column_t columns[5] = {
        {"Day", 15}, {"In", 21}, {"Start", 21}, {"End", 21}, {"Out", 22},
    };
    char subtitle[64];
    int32_t start = sunday(week_offset);
    int32_t current = today();
    week_label(start, subtitle, sizeof(subtitle));

    for (int row = 0; row < WEEK_ITEM_COUNT; ++row) clear_table_row(row);

    for (int i = 0; i < DAY_COUNT; ++i) {
        int32_t date_value = add_days(start, i);
        civil_t date = split_ymd(date_value);
        tc_day_t day = get_day(date_value);
        sadd(table_cells[i][0], sizeof(table_cells[i][0]), day_name(date_value));
        schar(table_cells[i][0], sizeof(table_cells[i][0]), ' ');
        suint(table_cells[i][0], sizeof(table_cells[i][0]), (uint32_t)date.day);
        if (date_value == current) schar(table_cells[i][0], sizeof(table_cells[i][0]), '*');
        for (int punch = 0; punch < PUNCH_COUNT; ++punch) {
            format_ampm(day.punches[punch], table_cells[i][punch + 1], sizeof(table_cells[i][punch + 1]));
        }
    }

    for (int punch = 0; punch < PUNCH_COUNT; ++punch) {
        int row = DAY_COUNT + punch;
        table_rows[row].flags = T5_UI_TABLE_ROW_FULL_WIDTH;
        scopy(table_cells[row][0], sizeof(table_cells[row][0]), punch_name((uint8_t)punch));
    }

    t5_ui_chrome_t page = chrome(subtitle, "Select");
    fwui->render_table(&page, columns, 5, table_rows, WEEK_ITEM_COUNT, selected);
}

static void render(void) {
    if (screen_id == SCREEN_WEEK_LIST) render_week_list();
    else if (screen_id == SCREEN_WEEK) render_week();
    else render_day();
}

static void open_week(int32_t offset) {
    week_offset = offset;
    screen_id = SCREEN_WEEK;
    selected = offset == 0 ? selected_today() : 0;
    editing_ymd = 0;
    set_status("Open a day, or punch below");
}

static void open_day(int32_t date) {
    tc_day_t day = get_day(date);
    editing_ymd = date;
    screen_id = SCREEN_DAY;
    selected = 0;
    set_status(any_punch(&day) ? "Confirm a row to edit" : "Confirm a row to set time");
}

static void punch_today(uint8_t punch) {
    int32_t date = today();
    int16_t minutes = (int16_t)now_minutes();
    if (!set_punch(date, punch, minutes)) {
        set_status("Could not save punch");
        return;
    }
    week_offset = 0;
    screen_id = SCREEN_WEEK;
    editing_ymd = 0;
    selected = selected_today();
    punch_status(punch, minutes);
}

static uint64_t make_cookie(int32_t date, uint8_t punch, int32_t offset) {
    uint64_t value = (uint32_t)date;
    value |= ((uint64_t)punch) << 32;
    value |= ((uint64_t)(uint16_t)(int16_t)offset) << 40;
    value |= ((uint64_t)COOKIE_MAGIC) << 56;
    return value;
}

static bool decode_cookie(uint64_t value, int32_t *date, uint8_t *punch, int32_t *offset) {
    if ((uint8_t)(value >> 56) != COOKIE_MAGIC) return false;
    if (date) *date = (int32_t)(uint32_t)value;
    if (punch) *punch = (uint8_t)(value >> 32);
    if (offset) *offset = (int32_t)(int16_t)(uint16_t)(value >> 40);
    return true;
}

static bool request_edit(void) {
    uint8_t punch = (uint8_t)selected;
    if (screen_id != SCREEN_DAY || punch >= PUNCH_COUNT) return false;
    tc_day_t day = get_day(editing_ymd);
    char initial[24];
    format_ampm(day.punches[punch], initial, sizeof(initial));
    if (day.punches[punch] < 0) initial[0] = 0;
    if (!system_ui->keyboard_request(punch_name(punch), initial, 12, T5_SYSTEM_KEYBOARD_TEXT,
                                     make_cookie(editing_ymd, punch, week_offset))) {
        set_status("Keyboard unavailable");
        return false;
    }
    return true;
}

static bool activate(void) {
    if (screen_id == SCREEN_WEEK_LIST) {
        open_week(-selected);
        return false;
    }
    if (screen_id == SCREEN_DAY) return request_edit();
    if (selected < DAY_COUNT) open_day(add_days(sunday(week_offset), selected));
    else punch_today((uint8_t)(selected - DAY_COUNT));
    return false;
}

static bool consume_keyboard(void) {
    char text[64];
    bool cancelled = false;
    uint64_t cookie = 0;
    int32_t date = 0, offset = 0;
    uint8_t punch = 0;
    int16_t minutes = -1;
    if (!system_ui->keyboard_take_result(text, sizeof(text), &cancelled, &cookie)) return false;
    if (!decode_cookie(cookie, &date, &punch, &offset) || punch >= PUNCH_COUNT || date < 19700101) {
        set_status("Keyboard result ignored");
        return true;
    }
    load_store();
    screen_id = SCREEN_DAY;
    week_offset = offset;
    selected = punch;
    editing_ymd = date;
    if (cancelled) {
        tc_day_t day = get_day(date);
        set_status(any_punch(&day) ? "Confirm a row to edit" : "Confirm a row to set time");
        return true;
    }
    if (!parse_time(text, &minutes) || !set_punch(date, punch, minutes)) {
        set_status("Edit time");
        return true;
    }
    punch_status(punch, minutes);
    return true;
}

static bool apis_ok(void) {
    const size_t app_required = offsetof(t5_app_api_v1, set_back_exits_app) + sizeof(app->set_back_exits_app);
    const size_t storage_required = offsetof(t5_storage_api_v1, write_file_atomic) + sizeof(storage->write_file_atomic);
    const size_t time_required = offsetof(t5_system_api_v1, local_datetime) + sizeof(system_api->local_datetime);
    const size_t system_ui_required =
        offsetof(t5_system_ui_api_v1, navigate_home) + sizeof(system_ui->navigate_home);
    const size_t ui_required = offsetof(t5_ui_api_v1, previous_index) + sizeof(fwui->previous_index);

    return app && app->abi_version == T5_APP_ABI_VERSION && app->struct_size >= app_required &&
           app->set_back_exits_app && storage && storage->api_version == T5_STORAGE_API_VERSION &&
           storage->struct_size >= storage_required && storage->exists && storage->read_file &&
           storage->write_file_atomic && system_api && system_api->api_version == T5_SYSTEM_API_VERSION &&
           system_api->struct_size >= time_required && system_api->local_datetime && system_ui &&
           system_ui->api_version == T5_SYSTEM_UI_API_VERSION && system_ui->struct_size >= system_ui_required &&
           system_ui->keyboard_request && system_ui->keyboard_take_result && system_ui->navigate_home && fwui &&
           fwui->api_version == T5_UI_API_VERSION && fwui->struct_size >= ui_required && fwui->render_list &&
           fwui->render_table && fwui->hit_test && fwui->poll_event && fwui->next_index && fwui->previous_index;
}

static void go_back(void) {
    if (screen_id == SCREEN_DAY) {
        screen_id = SCREEN_WEEK;
        editing_ymd = 0;
        selected = 0;
        set_status("Open a day, or punch below");
        return;
    }
    if (screen_id == SCREEN_WEEK) {
        screen_id = SCREEN_WEEK_LIST;
        selected = -week_offset;
        if (selected < 0) selected = 0;
        if (selected >= WEEK_HISTORY) selected = WEEK_HISTORY - 1;
        set_status("Select a week");
        return;
    }
    app->set_back_exits_app(true);
    system_ui->navigate_home();
}

static bool handle_touch(int16_t x, int16_t y) {
    int32_t hit = fwui->hit_test(x, y);
    if (hit == T5_UI_HIT_HEADER) {
        if (screen_id != SCREEN_WEEK_LIST) {
            screen_id = SCREEN_WEEK_LIST;
            selected = -week_offset;
            if (selected < 0) selected = 0;
            if (selected >= WEEK_HISTORY) selected = WEEK_HISTORY - 1;
            editing_ymd = 0;
            set_status("Select a week");
        }
        return false;
    }
    if (hit < 0 || hit >= item_count()) return false;
    selected = hit;
    return activate();
}

__attribute__((visibility("default"))) void app_main(void) {
    app = t5_app_get_api(T5_APP_ABI_VERSION);
    storage = t5_storage_get_api(T5_STORAGE_API_VERSION);
    system_api = t5_system_get_api(T5_SYSTEM_API_VERSION);
    system_ui = t5_system_ui_get_api(T5_SYSTEM_UI_API_VERSION);
    fwui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!apis_ok()) return;

    app->set_back_exits_app(false);
    load_store();
    if (!consume_keyboard()) {
        screen_id = SCREEN_WEEK_LIST;
        week_offset = 0;
        selected = 0;
        editing_ymd = 0;
        set_status("Select a week");
    }
    render();

    t5_ui_event_t event;
    while (fwui->poll_event(&event, 20)) {
        if (event.type == T5_UI_EVENT_EXIT) break;
        if (event.type == T5_UI_EVENT_NONE) continue;

        if (event.type == T5_UI_EVENT_BACK) {
            const uint8_t before = screen_id;
            go_back();
            if (before == SCREEN_WEEK_LIST) return;
            render();
            continue;
        }

        if (event.type == T5_UI_EVENT_CONFIRM) {
            if (activate()) {
                app->set_back_exits_app(true);
                return;
            }
            render();
            continue;
        }

        if (event.type == T5_UI_EVENT_PREVIOUS) {
            selected = fwui->previous_index(selected, (uint32_t)item_count());
            render();
            continue;
        }

        if (event.type == T5_UI_EVENT_NEXT) {
            selected = fwui->next_index(selected, (uint32_t)item_count());
            render();
            continue;
        }

        if (event.type == T5_UI_EVENT_TAP) {
            if (handle_touch(event.touch_x, event.touch_y)) {
                app->set_back_exits_app(true);
                return;
            }
            render();
        }
    }

    app->set_back_exits_app(true);
}
