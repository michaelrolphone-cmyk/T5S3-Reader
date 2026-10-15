#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "T5AppApi.h"
#include "T5StorageApi.h"
#include "T5SystemApi.h"
#include "T5SystemUiApi.h"
#include "T5UiApi.h"

#include "../../Apps/timecard.c"

static bool file_present = true;
static bool read_ok;
static const char *store_fixture;
static char persisted[JSON_CAPACITY];
static int writes;
static int keyboard_requests;
static bool keyboard_result_ready;
static bool keyboard_cancelled;
static uint64_t keyboard_cookie;
static const char *keyboard_text = "";

static bool mock_local_datetime(t5_local_datetime_t *value) {
    assert(value != NULL);
    *value = (t5_local_datetime_t){
        .year = 2026, .month = 9, .day = 30, .hour = 8, .minute = 30,
    };
    return true;
}

static bool mock_exists(const char *path) {
    assert(strcmp(path, STORE_PATH) == 0);
    return file_present;
}

static bool mock_read(const char *path, void *buffer, size_t capacity, size_t *size_out) {
    assert(strcmp(path, STORE_PATH) == 0);
    if (size_out) *size_out = 0;
    if (!read_ok || !store_fixture) return false;
    size_t size = strlen(store_fixture);
    if (size >= capacity) return false;
    memcpy(buffer, store_fixture, size);
    if (size_out) *size_out = size;
    return true;
}

static bool mock_write(const char *path, const void *data, size_t size) {
    assert(strcmp(path, STORE_PATH) == 0);
    assert(data && size < sizeof(persisted));
    ++writes;
    memcpy(persisted, data, size);
    persisted[size] = 0;
    return true;
}

static bool mock_keyboard_request(const char *title, const char *initial, size_t maximum,
                                 uint8_t type, uint64_t cookie) {
    (void)title; (void)initial; (void)maximum; (void)type;
    ++keyboard_requests;
    keyboard_cookie = cookie;
    return true;
}

static bool mock_keyboard_take(char *text, size_t capacity, bool *cancelled, uint64_t *cookie) {
    if (!keyboard_result_ready) return false;
    keyboard_result_ready = false;
    assert(strlen(keyboard_text) < capacity);
    strcpy(text, keyboard_text);
    if (cancelled) *cancelled = keyboard_cancelled;
    if (cookie) *cookie = keyboard_cookie;
    return true;
}

static const t5_storage_api_v1 mock_storage = {
    .api_version = T5_STORAGE_API_VERSION,
    .struct_size = sizeof(t5_storage_api_v1),
    .exists = mock_exists,
    .read_file = mock_read,
    .write_file_atomic = mock_write,
};

static const t5_system_ui_api_v1 mock_system_ui = {
    .api_version = T5_SYSTEM_UI_API_VERSION,
    .struct_size = sizeof(t5_system_ui_api_v1),
    .keyboard_request = mock_keyboard_request,
    .keyboard_take_result = mock_keyboard_take,
};

static const t5_system_api_v1 mock_system = {
    .api_version = T5_SYSTEM_API_VERSION,
    .struct_size = sizeof(t5_system_api_v1),
    .local_datetime = mock_local_datetime,
};

const t5_app_api_v1 *t5_app_get_api(uint32_t version) { (void)version; return NULL; }
const t5_storage_api_v1 *t5_storage_get_api(uint32_t version) { (void)version; return NULL; }
const t5_system_api_v1 *t5_system_get_api(uint32_t version) { (void)version; return NULL; }
const t5_system_ui_api_v1 *t5_system_ui_get_api(uint32_t version) { (void)version; return NULL; }
const t5_ui_api_v1 *t5_ui_get_api(uint32_t version) { (void)version; return NULL; }

int main(void) {
    static const struct { const char *text; int16_t minutes; } valid[] = {
        {"", -1}, {" \t", -1}, {"0", 0}, {"00:00", 0}, {"23:59", 1439},
        {"9", 540}, {"9:5", 545}, {"9:05", 545}, {"09:30", 570},
        {"12 AM", 0}, {"12:00 PM", 720}, {"1pm", 780}, {"9:30 aM", 570},
        {" \t9:30 PM\t ", 1290}, {"23", 1380}
    };
    static const char *invalid[] = {
        "9:123", "09:30xyz", "9am pm", "9pm am", "9:60", "24:00", "13pm",
        "0am", "9:", ":30", "-1", "+9", "9.30", "9a", "9p", "9 amp",
        "9amjunk", "9:3x", "9:300", "9:30:00", "9 30", "0009", "99",
        "999999999999999999999999999999999999999999999999999999999999",
        "AM", "9 a m", "9 PM PM", "9\n", "9:30\r", "\xFF"
    };
    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i) {
        int16_t value = -123;
        assert(parse_time(valid[i].text, &value));
        assert(value == valid[i].minutes);
    }
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        int16_t value = -123;
        assert(!parse_time(invalid[i], &value));
        assert(value == -123);
    }
    int16_t value = -123;
    assert(!parse_time(NULL, &value) && value == -123);
    assert(!parse_time("9", NULL));
    /* Every displayed minute must be accepted without changing its value. */
    for (int16_t minute = 0; minute < 1440; ++minute) {
        char text[24];
        format_ampm(minute, text, sizeof(text));
        assert(parse_time(text, &value) && value == minute);
    }
    storage = &mock_storage;
    system_ui = &mock_system_ui;
    system_api = &mock_system;
    store_fixture = "{\"days\":[{\"d\":20260929,\"in\":495,\"out\":1035}]}";
    read_ok = true;
    assert(load_store());
    tc_day_t before = days[0];
    keyboard_cookie = make_cookie(20260929, 0, 0);
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        keyboard_text = invalid[i];
        keyboard_result_ready = true;
        assert(consume_keyboard());
        assert(writes == 0 && day_count == 1 && memcmp(days, &before, sizeof(before)) == 0);
        assert(strcmp(status_text, "Edit time") == 0);
    }
    /* A rejected edit does not strand the handoff; cancel and valid retry work. */
    keyboard_text = "9:30 PM";
    keyboard_cancelled = true;
    keyboard_result_ready = true;
    assert(consume_keyboard() && writes == 0);
    keyboard_cancelled = false;
    keyboard_result_ready = true;
    assert(consume_keyboard() && writes == 1);
    assert(days[0].punches[0] == 1290 && strstr(persisted, "1290"));
    assert(!consume_keyboard() && writes == 1);
    /* Empty input retains the existing explicit clear-punch behavior. */
    keyboard_text = "";
    keyboard_result_ready = true;
    assert(consume_keyboard() && writes == 2 && days[0].punches[0] == -1);
    return 0;
}
