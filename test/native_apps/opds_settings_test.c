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
static uint32_t input_script[8];
static size_t script_count;
static size_t script_index;
static int renders;
static int keyboard_requests;
static int updates;
static int adds;
static bool keyboard_pending;
static bool keyboard_cancelled;
static bool expect_url_keyboard;
static const char *keyboard_text = "Library";
static uint64_t pending_cookie;

static bool app_poll(t5_app_input_t *input, uint32_t wait_ms) {
    assert(input && wait_ms == 50);
    memset(input, 0, sizeof(*input));
    if (script_index < script_count) input->buttons = input_script[script_index++];
    else input->exit_requested = true;
    return true;
}

static void set_script(const uint32_t *buttons, size_t count) {
    assert(count <= sizeof(input_script) / sizeof(input_script[0]));
    memset(input_script, 0, sizeof(input_script));
    if (count) memcpy(input_script, buttons, count * sizeof(buttons[0]));
    script_count = count;
    script_index = 0;
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
    ++adds;
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
    if (expect_url_keyboard) {
        assert(strcmp(title, "OPDS Server URL") == 0);
        assert(strcmp(initial, "https://") == 0);
        assert(max_length == 127);
        assert(input_type == T5_SYSTEM_KEYBOARD_URL);
    } else {
        assert(strcmp(title, "Server Name") == 0);
        assert(strcmp(initial, "Calibre") == 0);
        assert(max_length == 63);
        assert(input_type == T5_SYSTEM_KEYBOARD_TEXT);
    }
    pending_cookie = cookie;
    ++keyboard_requests;
    return true;
}
static bool keyboard_take_result(char *text, size_t capacity, bool *cancelled, uint64_t *cookie) {
    if (!keyboard_pending) return false;
    assert(text && capacity >= 8);
    strcpy(text, keyboard_text);
    if (cancelled) *cancelled = keyboard_cancelled;
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
        assert(row_count == stored_count + 1);
        assert(strcmp(rows[0].title, stored[0].name) == 0);
        assert(strcmp(rows[0].subtitle, stored[0].url) == 0);
        assert(strcmp(rows[stored_count].title, "Add Server") == 0);
    } else {
        if (strcmp(chrome->title, "Add Server") == 0) {
            assert(row_count == 4);
            assert(strcmp(rows[0].title, "Server Name") == 0);
        } else {
            assert(strcmp(chrome->title, "OPDS Server") == 0);
            assert(row_count == 5);
            assert(strcmp(rows[0].title, "Server Name") == 0);
        }
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

    const uint32_t open_and_request_name[] = {T5_APP_BUTTON_CONFIRM, T5_APP_BUTTON_CONFIRM};
    set_script(open_and_request_name, sizeof(open_and_request_name) / sizeof(open_and_request_name[0]));
    app_main();
    assert(keyboard_requests == 1);
    assert(updates == 0 && adds == 0);

    // Canceling an existing-server edit must not persist the canceled value.
    keyboard_pending = true;
    keyboard_cancelled = true;
    const uint32_t back_out[] = {T5_APP_BUTTON_BACK};
    set_script(back_out, sizeof(back_out) / sizeof(back_out[0]));
    app_main();
    assert(updates == 0);
    assert(strcmp(stored[0].name, "Calibre") == 0);

    // Retry with a successful keyboard result; the update is then persisted.
    keyboard_pending = false;
    keyboard_cancelled = false;
    const uint32_t reopen_and_request_name[] = {T5_APP_BUTTON_CONFIRM, T5_APP_BUTTON_CONFIRM};
    set_script(reopen_and_request_name, sizeof(reopen_and_request_name) / sizeof(reopen_and_request_name[0]));
    app_main();
    assert(keyboard_requests == 2);
    keyboard_pending = true;
    set_script(back_out, sizeof(back_out) / sizeof(back_out[0]));
    app_main();
    assert(updates == 1);
    assert(strcmp(stored[0].name, "Library") == 0);

    // Open Add Server, request its URL, cancel, then retry with a valid URL.
    expect_url_keyboard = true;
    const uint32_t open_add_and_request_url[] = {
        T5_APP_BUTTON_DOWN, T5_APP_BUTTON_CONFIRM, T5_APP_BUTTON_DOWN, T5_APP_BUTTON_CONFIRM};
    set_script(open_add_and_request_url,
               sizeof(open_add_and_request_url) / sizeof(open_add_and_request_url[0]));
    keyboard_pending = false;
    app_main();
    assert(keyboard_requests == 3);
    keyboard_pending = true;
    keyboard_cancelled = true;
    set_script(back_out, sizeof(back_out) / sizeof(back_out[0]));
    app_main();
    assert(adds == 0 && stored_count == 1);

    keyboard_pending = false;
    const uint32_t reopen_add_and_request_url[] = {
        T5_APP_BUTTON_DOWN, T5_APP_BUTTON_CONFIRM, T5_APP_BUTTON_DOWN, T5_APP_BUTTON_CONFIRM};
    set_script(reopen_add_and_request_url,
               sizeof(reopen_add_and_request_url) / sizeof(reopen_add_and_request_url[0]));
    app_main();
    assert(keyboard_requests == 4);
    keyboard_pending = true;
    keyboard_cancelled = false;
    keyboard_text = "https://books.example/opds";
    set_script(back_out, sizeof(back_out) / sizeof(back_out[0]));
    app_main();
    assert(adds == 1 && stored_count == 2);
    assert(strcmp(stored[1].url, "https://books.example/opds") == 0);
    assert(renders >= 10);
    return 0;
}
