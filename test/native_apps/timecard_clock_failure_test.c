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

static bool clock_available;
static int clock_reads;
static int store_writes;
static bool rollover;
static bool save_available = true;
static char persisted[JSON_CAPACITY];

static bool mock_local_datetime(t5_local_datetime_t *value) {
    ++clock_reads;
    if (!clock_available) return false;
    *value = (t5_local_datetime_t){
        .year = 2026,
        .month = 9,
        .day = 30,
        .hour = 14,
        .minute = 37,
    };
    if (rollover) {
        value->day = clock_reads == 1 ? 30 : 1;
        value->month = clock_reads == 1 ? 9 : 10;
        value->hour = clock_reads == 1 ? 23 : 0;
        value->minute = clock_reads == 1 ? 59 : 0;
    }
    return true;
}

static bool mock_write_file(const char *path, const void *data, size_t size) {
    assert(strcmp(path, STORE_PATH) == 0);
    assert(data != NULL);
    assert(size > 0);
    ++store_writes;
    if (!save_available) return false;
    assert(size < sizeof(persisted));
    memcpy(persisted, data, size);
    persisted[size] = 0;
    return true;
}

static const t5_system_api_v1 mock_system_api = {
    .api_version = T5_SYSTEM_API_VERSION,
    .struct_size = sizeof(t5_system_api_v1),
    .local_datetime = mock_local_datetime,
};

static const t5_storage_api_v1 mock_storage_api = {
    .api_version = T5_STORAGE_API_VERSION,
    .struct_size = sizeof(t5_storage_api_v1),
    .write_file_atomic = mock_write_file,
};

const t5_app_api_v1 *t5_app_get_api(uint32_t version) {
    (void)version;
    return NULL;
}

const t5_storage_api_v1 *t5_storage_get_api(uint32_t version) {
    (void)version;
    return NULL;
}

const t5_system_api_v1 *t5_system_get_api(uint32_t version) {
    (void)version;
    return NULL;
}

const t5_system_ui_api_v1 *t5_system_ui_get_api(uint32_t version) {
    (void)version;
    return NULL;
}

const t5_ui_api_v1 *t5_ui_get_api(uint32_t version) {
    (void)version;
    return NULL;
}

int main(void) {
    system_api = &mock_system_api;
    storage = &mock_storage_api;
    store_ready = true; // The existing clock scenarios operate on a successfully loaded store.
    for (uint8_t punch = 0; punch < PUNCH_COUNT; ++punch) {
        day_count = 1;
        days[0] = blank_day(20260929);
        days[0].punches[0] = 480;
        tc_day_t prior = days[0];
        screen_id = SCREEN_WEEK;
        week_offset = -2;
        selected = DAY_COUNT + punch;
        clock_reads = store_writes = 0;
        clock_available = false;
        for (int retry = 0; retry < 3; ++retry) {
            assert(!activate());
            assert(clock_reads == retry + 1);
            assert(store_writes == 0 && day_count == 1);
            assert(memcmp(&days[0], &prior, sizeof(prior)) == 0);
            assert(strcmp(status_text, "Clock unavailable") == 0);
            assert(screen_id == SCREEN_WEEK && week_offset == -2);
        }
        go_back(); // Cancel after failure never writes.
        assert(screen_id == SCREEN_WEEK_LIST && store_writes == 0);
        screen_id = SCREEN_WEEK;
        selected = DAY_COUNT + punch;
        clock_available = true;
        assert(!activate());
        assert(store_writes == 1 && day_count == 2);
        assert(days[1].ymd == 20260930);
        assert(days[1].punches[punch] == 14 * 60 + 37);
        assert(strstr(persisted, "19700101") == NULL);
        assert(memcmp(&days[0], &prior, sizeof(prior)) == 0);

        // A date boundary after the first read cannot pair yesterday with 00:00.
        day_count = clock_reads = store_writes = 0;
        rollover = true;
        punch_today(punch);
        assert(store_writes == 1 && day_count == 1);
        assert(days[0].ymd == 20260930);
        assert(days[0].punches[punch] == 1439);
        rollover = false;

        // Persistence failure stays visible and a subsequent retry can save.
        save_available = false;
        punch_today(punch);
        assert(strcmp(status_text, "Could not save punch") == 0);
        save_available = true;
        punch_today(punch);
        assert(days[0].punches[punch] == 14 * 60 + 37);
    }
    return 0;
}
