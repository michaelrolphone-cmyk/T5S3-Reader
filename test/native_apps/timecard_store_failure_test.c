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
    if (text && capacity) text[0] = 0;
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
    storage = &mock_storage;
    system_ui = &mock_system_ui;
    system_api = &mock_system;
    const char valid_store[] =
        "{\"days\":[{\"d\":20260928,\"in\":480,\"out\":1020},"
        "{\"d\":20260929,\"in\":495,\"out\":1035}]}";
    const char malformed_store[] =
        "{\"days\":[{\"d\":20260928,\"in\":480},"
        "{\"d\":20260929,\"in\":495";
    tc_day_t before[2];

    /* A transient read failure must preserve loaded history and disable writes. */
    store_fixture = valid_store;
    read_ok = true;
    assert(load_store());
    memcpy(before, days, sizeof(before));
    read_ok = false;
    writes = 0;
    assert(!load_store());
    assert(day_count == 2 && memcmp(days, before, sizeof(before)) == 0);
    assert(!set_punch(20260930, 0, 510));
    assert(writes == 0 && day_count == 2 && memcmp(days, before, sizeof(before)) == 0);
    punch_today(0);
    assert(writes == 0 && strcmp(status_text, "History unavailable; read-only") == 0);
    assert(!request_edit());
    assert(keyboard_requests == 0);
    assert(strcmp(status_text, "History unavailable; read-only") == 0);

    /* A parse error after a valid record must not publish a partial history. */
    read_ok = true;
    store_fixture = malformed_store;
    assert(!load_store());
    assert(day_count == 2 && memcmp(days, before, sizeof(before)) == 0);
    assert(!set_punch(20260930, 1, 600));
    assert(writes == 0 && memcmp(days, before, sizeof(before)) == 0);
    keyboard_cookie = make_cookie(20260929, 0, 0);
    keyboard_result_ready = true;
    keyboard_cancelled = false;
    assert(consume_keyboard());
    assert(screen_id == SCREEN_WEEK_LIST && writes == 0);
    assert(strcmp(status_text, "History unavailable; read-only") == 0);

    /* A later clean read restores editing; canceling the keyboard writes nothing. */
    store_fixture = valid_store;
    assert(load_store());
    assert(day_count == 2);
    screen_id = SCREEN_DAY;
    editing_ymd = 20260929;
    selected = 0;
    assert(request_edit());
    assert(keyboard_requests == 1);
    keyboard_result_ready = true;
    keyboard_cancelled = true;
    assert(consume_keyboard());
    assert(writes == 0 && day_count == 2);

    /* A successful retry after an initial read error may commit a new punch. */
    read_ok = false;
    assert(!load_store());
    assert(!set_punch(20260930, 2, 720));
    assert(writes == 0);
    read_ok = true;
    assert(load_store());
    assert(set_punch(20260930, 2, 720));
    assert(writes == 1 && strstr(persisted, "20260930") != NULL);
    return 0;
}
