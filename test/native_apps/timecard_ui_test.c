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

void app_main(void);

static int list_renders;
static int table_renders;
static int event_index;
static bool back_exits = true;
static bool navigated_home;

static void set_back_exits(bool enabled) { back_exits = enabled; }

static bool storage_exists(const char *path) {
    assert(path != NULL);
    return false;
}

static bool storage_read(const char *path, void *buffer, size_t capacity, size_t *size_out) {
    (void)path;
    (void)buffer;
    (void)capacity;
    if (size_out) *size_out = 0;
    return false;
}

static bool storage_write(const char *path, const void *data, size_t size) {
    (void)path;
    (void)data;
    (void)size;
    return true;
}

static bool storage_remove(const char *path) {
    (void)path;
    return true;
}

static bool local_datetime(t5_local_datetime_t *value) {
    assert(value != NULL);
    *value = (t5_local_datetime_t){
        .year = 2026,
        .month = 9,
        .day = 13,
        .hour = 8,
        .minute = 30,
        .second = 0,
        .weekday = 0,
        .yearday = 255,
    };
    return true;
}

static bool keyboard_request(const char *title, const char *initial_text, size_t max_length,
                             uint8_t input_type, uint64_t cookie) {
    (void)title;
    (void)initial_text;
    (void)max_length;
    (void)input_type;
    (void)cookie;
    assert(!"keyboard should not be requested by this navigation test");
    return false;
}

static bool keyboard_take_result(char *text, size_t capacity, bool *cancelled, uint64_t *cookie) {
    (void)text;
    (void)capacity;
    (void)cancelled;
    (void)cookie;
    return false;
}

static void navigate_home(void) { navigated_home = true; }

static void render_list(const t5_ui_chrome_t *chrome, const t5_ui_list_row_t *rows,
                        uint32_t row_count, int32_t selected_index) {
    assert(chrome != NULL);
    assert(rows != NULL);
    assert(strcmp(chrome->title, "Time Card") == 0);

    ++list_renders;
    if (list_renders == 1) {
        assert(strcmp(chrome->subtitle, "Weeks") == 0);
        assert(strcmp(chrome->confirm_label, "Open") == 0);
        assert(strcmp(chrome->back_label, "Home") == 0);
        assert(row_count == 20);
        assert(selected_index == 0);
        assert(strncmp(rows[0].title, "Week ", 5) == 0);
        assert(strcmp(rows[0].subtitle, "This week") == 0);
    } else {
        assert(strcmp(chrome->subtitle, "Weeks") == 0);
        assert(row_count == 20);
        assert(selected_index == 0);
    }
}

static void render_table(const t5_ui_chrome_t *chrome, const t5_ui_table_column_t *columns,
                         uint32_t column_count, const t5_ui_table_row_t *rows,
                         uint32_t row_count, int32_t selected_index) {
    assert(chrome != NULL);
    assert(columns != NULL);
    assert(rows != NULL);
    assert(strcmp(chrome->title, "Time Card") == 0);
    assert(strcmp(chrome->confirm_label, "Select") == 0);
    assert(strcmp(chrome->back_label, "Back") == 0);
    assert(column_count == 5);
    assert(strcmp(columns[0].title, "Day") == 0);
    assert(strcmp(columns[1].title, "In") == 0);
    assert(strcmp(columns[2].title, "Start") == 0);
    assert(strcmp(columns[3].title, "End") == 0);
    assert(strcmp(columns[4].title, "Out") == 0);
    assert(row_count == 11);
    assert(strcmp(rows[0].cells[0], "Sun 13*") == 0);
    assert(strcmp(rows[0].cells[1], "--") == 0);
    assert(strcmp(rows[7].cells[0], "Clock in") == 0);
    assert((rows[7].flags & T5_UI_TABLE_ROW_FULL_WIDTH) != 0);

    ++table_renders;
    assert(selected_index == (table_renders == 1 ? 0 : 1));
}

static int32_t hit_test(int16_t x, int16_t y) {
    (void)x;
    (void)y;
    return T5_UI_HIT_NONE;
}

static bool poll_event(t5_ui_event_t *event, uint32_t wait_ms) {
    (void)wait_ms;
    assert(event != NULL);
    static const uint8_t events[] = {
        T5_UI_EVENT_CONFIRM,
        T5_UI_EVENT_NEXT,
        T5_UI_EVENT_BACK,
        T5_UI_EVENT_EXIT,
    };
    if (event_index >= (int)(sizeof(events) / sizeof(events[0]))) return false;
    *event = (t5_ui_event_t){.type = events[event_index++]};
    return true;
}

static int32_t next_index(int32_t current, uint32_t count) {
    return count ? (current + 1) % (int32_t)count : 0;
}

static int32_t previous_index(int32_t current, uint32_t count) {
    return count ? (current + (int32_t)count - 1) % (int32_t)count : 0;
}

static const t5_app_api_v1 app_api = {
    .abi_version = T5_APP_ABI_VERSION,
    .struct_size = sizeof(t5_app_api_v1),
    .set_back_exits_app = set_back_exits,
};

static const t5_storage_api_v1 storage_api = {
    .api_version = T5_STORAGE_API_VERSION,
    .struct_size = sizeof(t5_storage_api_v1),
    .exists = storage_exists,
    .read_file = storage_read,
    .write_file_atomic = storage_write,
    .remove_file = storage_remove,
};

static const t5_system_api_v1 system_api = {
    .api_version = T5_SYSTEM_API_VERSION,
    .struct_size = sizeof(t5_system_api_v1),
    .local_datetime = local_datetime,
};

static const t5_system_ui_api_v1 system_ui_api = {
    .api_version = T5_SYSTEM_UI_API_VERSION,
    .struct_size = sizeof(t5_system_ui_api_v1),
    .keyboard_request = keyboard_request,
    .keyboard_take_result = keyboard_take_result,
    .navigate_home = navigate_home,
};

static const t5_ui_api_v1 ui_api = {
    .api_version = T5_UI_API_VERSION,
    .struct_size = sizeof(t5_ui_api_v1),
    .render_list = render_list,
    .render_table = render_table,
    .hit_test = hit_test,
    .poll_event = poll_event,
    .next_index = next_index,
    .previous_index = previous_index,
};

const t5_app_api_v1 *t5_app_get_api(uint32_t version) {
    return version == T5_APP_ABI_VERSION ? &app_api : NULL;
}

const t5_storage_api_v1 *t5_storage_get_api(uint32_t version) {
    return version == T5_STORAGE_API_VERSION ? &storage_api : NULL;
}

const t5_system_api_v1 *t5_system_get_api(uint32_t version) {
    return version == T5_SYSTEM_API_VERSION ? &system_api : NULL;
}

const t5_system_ui_api_v1 *t5_system_ui_get_api(uint32_t version) {
    return version == T5_SYSTEM_UI_API_VERSION ? &system_ui_api : NULL;
}

const t5_ui_api_v1 *t5_ui_get_api(uint32_t version) {
    return version == T5_UI_API_VERSION ? &ui_api : NULL;
}

int main(void) {
    app_main();
    assert(list_renders == 2);
    assert(table_renders == 2);
    assert(event_index == 4);
    assert(back_exits);
    assert(!navigated_home);
    return 0;
}
