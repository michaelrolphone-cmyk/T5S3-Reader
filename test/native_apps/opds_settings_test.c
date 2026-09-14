#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "T5AppApi.h"
#include "T5OpdsApi.h"
#include "T5SystemUiApi.h"
#include "T5UiApi.h"

void app_main(void);

static t5_opds_server_t stored[8];
static uint32_t stored_count = 1;
static int poll_step;
static int renders;
static int keyboard_requests;
static int updates;
static bool keyboard_pending;
static uint64_t pending_cookie;

static bool app_poll(t5_app_input_t *input, uint32_t wait_ms) {
    assert(input && wait_ms == 50);
    memset(input, 0, sizeof(*input));
    ++poll_step;
    if (!keyboard_pending && keyboard_requests == 0) {
        // Open first server, then edit its Name field.
        input->buttons = T5_APP_BUTTON_CONFIRM;
    } else {
        // After applying the keyboard result: Back to list, then Back out.
        input->buttons = T5_APP_BUTTON_BACK;
    }
    return true;
}

static const t5_app_api_v1 app_api = {
    .abi_version = T5_APP_ABI_VERSION,
    .struct_size = sizeof(t5_app_api_v1),
    .poll = app_poll,
};
const t5_app_api_v1 *t5_app_get_api(uint32_t version) {
    return version == T5_APP_ABI_VERSION ? &app_api : NULL;
}

static uint32_t opds_count(void) { return stored_count; }
static bool opds_read(uint32_t index, t5_opds_server_t *out) {
    if (!out || index >= stored_count) return false;
    *out = stored[index];
    return true;
}
static bool opds_add(const t5_opds_server_t *server, uint32_t *new_index) {
    if (!server || stored_count >= 8) return false;
    stored[stored_count] = *server;
    if (new_index) *new_index = stored_count;
    ++stored_count;
    return true;
}
static bool opds_update(uint32_t index, const t5_opds_server_t *server) {
    if (!server || index >= stored_count) return false;
    stored[index] = *server;
    ++updates;
    return true;
}
static bool opds_remove(uint32_t index) {
    if (index >= stored_count) return false;
    for (uint32_t i = index + 1; i < stored_count; ++i) stored[i - 1] = stored[i];
    --stored_count;
    return true;
}
static const t5_opds_api_v1 opds_api = {
    .api_version = T5_OPDS_API_VERSION,
    .struct_size = sizeof(t5_opds_api_v1),
    .count = opds_count,
    .read = opds_read,
    .add = opds_add,
    .update = opds_update,
    .remove = opds_remove,
};
const t5_opds_api_v1 *t5_opds_get_api(uint32_t version) {
    return version == T5_OPDS_API_VERSION ? &opds_api : NULL;
}

static bool keyboard_request(const char *title, const char *initial, size_t max_length,
                             uint8_t input_type, uint64_t cookie) {
    assert(strcmp(title, "Server Name") == 0);
    assert(strcmp(initial, "Calibre") == 0);
    assert(max_length == 63);
    assert(input_type == T5_SYSTEM_KEYBOARD_TEXT);
    pending_cookie = cookie;
    ++keyboard_requests;
    return true;
}
static bool keyboard_take_result(char *text, size_t capacity, bool *cancelled, uint64_t *cookie) {
    if (!keyboard_pending) return false;
    assert(text && capacity >= 8);
    strcpy(text, "Library");
    if (cancelled) *cancelled = false;
    if (cookie) *cookie = pending_cookie;
    keyboard_pending = false;
    return true;
}
static const t5_system_ui_api_v1 system_ui_api = {
    .api_version = T5_SYSTEM_UI_API_VERSION,
    .struct_size = sizeof(t5_system_ui_api_v1),
    .keyboard_request = keyboard_request,
    .keyboard_take_result = keyboard_take_result,
};
const t5_system_ui_api_v1 *t5_system_ui_get_api(uint32_t version) {
    return version == T5_SYSTEM_UI_API_VERSION ? &system_ui_api : NULL;
}

static void render_list(const t5_ui_chrome_t *chrome, const t5_ui_list_row_t *rows,
                        uint32_t row_count, int32_t selected_index) {
    assert(chrome && rows && selected_index >= 0);
    if (strcmp(chrome->title, "OPDS Servers") == 0) {
        assert(row_count == 2);
        assert(strcmp(rows[0].title, stored[0].name) == 0);
        assert(strcmp(rows[0].subtitle, stored[0].url) == 0);
        assert(strcmp(rows[1].title, "Add Server") == 0);
    } else {
        assert(strcmp(chrome->title, "OPDS Server") == 0);
        assert(row_count == 5);
        assert(strcmp(rows[0].title, "Server Name") == 0);
    }
    ++renders;
}
static int32_t hit_test(int16_t x, int16_t y) { (void)x; (void)y; return T5_UI_HIT_NONE; }
static int32_t next_index(int32_t current, uint32_t count) { return (current + 1) % (int32_t)count; }
static int32_t previous_index(int32_t current, uint32_t count) {
    return current <= 0 ? (int32_t)count - 1 : current - 1;
}
static const t5_ui_api_v1 ui_api = {
    .api_version = T5_UI_API_VERSION,
    .struct_size = sizeof(t5_ui_api_v1),
    .render_list = render_list,
    .hit_test = hit_test,
    .next_index = next_index,
    .previous_index = previous_index,
};
const t5_ui_api_v1 *t5_ui_get_api(uint32_t version) {
    return version == T5_UI_API_VERSION ? &ui_api : NULL;
}

int main(void) {
    memset(stored, 0, sizeof(stored));
    strcpy(stored[0].name, "Calibre");
    strcpy(stored[0].url, "https://books.example/opds");
    strcpy(stored[0].username, "alice");
    strcpy(stored[0].password, "secret");

    poll_step = 0;
    app_main();
    assert(keyboard_requests == 1);
    assert(updates == 0);

    keyboard_pending = true;
    poll_step = 0;
    app_main();
    assert(updates == 1);
    assert(strcmp(stored[0].name, "Library") == 0);
    assert(renders >= 4);
    return 0;
}
