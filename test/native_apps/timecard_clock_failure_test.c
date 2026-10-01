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
    return true;
}

static bool mock_write_file(const char *path, const void *data, size_t size) {
    assert(strcmp(path, STORE_PATH) == 0);
    assert(data != NULL);
    assert(size > 0);
    ++store_writes;
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
    day_count = 0;
    status_text[0] = 0;

    clock_available = false;
    punch_today(0);
    assert(clock_reads == 1);
    assert(store_writes == 0);
    assert(day_count == 0);
    assert(strcmp(status_text, "Clock unavailable") == 0);

    clock_available = true;
    punch_today(1);
    /* One snapshot is used for the punch; two later reads select today's row. */
    assert(clock_reads == 4);
    assert(store_writes == 1);
    assert(day_count == 1);
    assert(days[0].ymd == 20260930);
    assert(days[0].punches[1] == 14 * 60 + 37);
    return 0;
}
