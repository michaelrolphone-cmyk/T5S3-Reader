#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "T5AppApi.h"
#include "T5KOReaderApi.h"
#include "T5NetworkApi.h"
#include "T5SystemUiApi.h"
#include "T5UiApi.h"

void app_main(void);

#define COOKIE_USERNAME 0x4B4F55534552ULL
#define COOKIE_AUTH_WIFI 0x4B4F57494649ULL

enum scenario_t {
    SCENARIO_MATCH_TOGGLE,
    SCENARIO_KEYBOARD_REQUEST,
    SCENARIO_BACK_ONLY,
    SCENARIO_WIFI_REQUEST,
    SCENARIO_AUTH_DISMISS,
};

static enum scenario_t scenario;
static int poll_step;
static int settings_renders;
static int auth_renders;
static int username_saves;
static int match_saves;
static int auth_calls;
static int auth_end_calls;
static int keyboard_requests;
static int wifi_requests;
static bool wifi_connected_value;
static bool keyboard_pending;
static bool wifi_pending;
static char stored_username[65] = "alice";
static char stored_password[65] = "secret";
static char stored_server[129] = "";
static uint8_t stored_match = T5_KOREADER_MATCH_FILENAME;

static bool app_poll(t5_app_input_t *input, uint32_t wait_ms) {
    assert(input && wait_ms == 50);
    memset(input, 0, sizeof(*input));
    ++poll_step;
    switch (scenario) {
        case SCENARIO_MATCH_TOGGLE:
            if (poll_step <= 3) input->buttons = T5_APP_BUTTON_DOWN;
            else if (poll_step == 4) input->buttons = T5_APP_BUTTON_CONFIRM;
            else input->buttons = T5_APP_BUTTON_BACK;
            break;
        case SCENARIO_KEYBOARD_REQUEST:
            input->buttons = T5_APP_BUTTON_CONFIRM;
            break;
        case SCENARIO_WIFI_REQUEST:
            if (poll_step <= 4) input->buttons = T5_APP_BUTTON_DOWN;
            else input->buttons = T5_APP_BUTTON_CONFIRM;
            break;
        case SCENARIO_AUTH_DISMISS:
            input->buttons = poll_step == 1 ? T5_APP_BUTTON_CONFIRM : T5_APP_BUTTON_BACK;
            break;
        case SCENARIO_BACK_ONLY:
        default:
            input->buttons = T5_APP_BUTTON_BACK;
            break;
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

static bool read_settings(t5_koreader_settings_t *out) {
    assert(out);
    memset(out, 0, sizeof(*out));
    strcpy(out->username, stored_username);
    strcpy(out->password, stored_password);
    strcpy(out->server_url, stored_server);
    out->match_method = stored_match;
    out->has_credentials = stored_username[0] && stored_password[0];
    return true;
}

static bool set_username(const char *value) {
    assert(value);
    strcpy(stored_username, value);
    ++username_saves;
    return true;
}
static bool set_password(const char *value) { (void)value; return true; }
static bool set_server_url(const char *value) { (void)value; return true; }
static bool set_match_method(uint8_t value) {
    assert(value <= T5_KOREADER_MATCH_BINARY);
    stored_match = value;
    ++match_saves;
    return true;
}
static bool authenticate(t5_koreader_auth_result_t *out) {
    assert(out);
    memset(out, 0, sizeof(*out));
    out->status = T5_KOREADER_AUTH_OK;
    out->http_status = 200;
    strcpy(out->message, "OK");
    ++auth_calls;
    return true;
}
static void end_auth_session(void) { ++auth_end_calls; }

static const t5_koreader_api_v1 koreader_api = {
    .api_version = T5_KOREADER_API_VERSION,
    .struct_size = sizeof(t5_koreader_api_v1),
    .read_settings = read_settings,
    .set_username = set_username,
    .set_password = set_password,
    .set_server_url = set_server_url,
    .set_match_method = set_match_method,
    .authenticate = authenticate,
    .end_auth_session = end_auth_session,
};

const t5_koreader_api_v1 *t5_koreader_get_api(uint32_t version) {
    return version == T5_KOREADER_API_VERSION ? &koreader_api : NULL;
}

static bool network_connected(void) { return wifi_connected_value; }
static const t5_network_api_v1 network_api = {
    .api_version = T5_NETWORK_API_VERSION,
    .struct_size = sizeof(t5_network_api_v1),
    .wifi_connected = network_connected,
};
const t5_network_api_v1 *t5_network_get_api(uint32_t version) {
    return version == T5_NETWORK_API_VERSION ? &network_api : NULL;
}

static bool keyboard_request(const char *title, const char *initial_text, size_t max_length,
                             uint8_t input_type, uint64_t cookie) {
    assert(title && initial_text);
    assert(strcmp(title, "KOReader Username") == 0);
    assert(strcmp(initial_text, stored_username) == 0);
    assert(max_length == 64);
    assert(input_type == T5_SYSTEM_KEYBOARD_TEXT);
    assert(cookie == COOKIE_USERNAME);
    ++keyboard_requests;
    return true;
}
static bool keyboard_take_result(char *text, size_t capacity, bool *cancelled, uint64_t *cookie) {
    if (!keyboard_pending) return false;
    assert(text && capacity >= 4);
    strcpy(text, "bob");
    if (cancelled) *cancelled = false;
    if (cookie) *cookie = COOKIE_USERNAME;
    keyboard_pending = false;
    return true;
}
static bool wifi_request(uint64_t cookie) {
    assert(cookie == COOKIE_AUTH_WIFI);
    ++wifi_requests;
    return true;
}
static bool wifi_take_result(bool *connected, bool *cancelled, uint64_t *cookie) {
    if (!wifi_pending) return false;
    if (connected) *connected = true;
    if (cancelled) *cancelled = false;
    if (cookie) *cookie = COOKIE_AUTH_WIFI;
    wifi_pending = false;
    wifi_connected_value = true;
    return true;
}

static const t5_system_ui_api_v1 system_ui_api = {
    .api_version = T5_SYSTEM_UI_API_VERSION,
    .struct_size = sizeof(t5_system_ui_api_v1),
    .keyboard_request = keyboard_request,
    .keyboard_take_result = keyboard_take_result,
    .wifi_request = wifi_request,
    .wifi_take_result = wifi_take_result,
};
const t5_system_ui_api_v1 *t5_system_ui_get_api(uint32_t version) {
    return version == T5_SYSTEM_UI_API_VERSION ? &system_ui_api : NULL;
}

static void render_list(const t5_ui_chrome_t *chrome, const t5_ui_list_row_t *rows,
                        uint32_t row_count, int32_t selected_index) {
    assert(chrome && rows);
    if (strcmp(chrome->title, "KOReader Sync") == 0) {
        assert(row_count == 5);
        assert(selected_index >= 0 && selected_index < 5);
        assert(strcmp(rows[0].title, "Username") == 0);
        assert(strcmp(rows[1].title, "Password") == 0);
        assert(strcmp(rows[1].value, "******") == 0);
        assert(strcmp(rows[2].value, "Default") == 0);
        assert(strcmp(rows[3].title, "Document Matching") == 0);
        assert(strcmp(rows[4].title, "Authenticate") == 0);
        ++settings_renders;
        return;
    }
    assert(strcmp(chrome->title, "KOReader Authentication") == 0);
    assert(row_count == 1 && selected_index == 0);
    assert(strcmp(rows[0].value, "Success") == 0);
    ++auth_renders;
}
static int32_t hit_test(int16_t x, int16_t y) { (void)x; (void)y; return T5_UI_HIT_NONE; }
static int32_t next_index(int32_t current, uint32_t count) {
    assert(count == 5);
    return (current + 1) % (int32_t)count;
}
static int32_t previous_index(int32_t current, uint32_t count) {
    assert(count == 5);
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

static void run(enum scenario_t next) {
    scenario = next;
    poll_step = 0;
    app_main();
}

int main(void) {
    run(SCENARIO_MATCH_TOGGLE);
    assert(match_saves == 1);
    assert(stored_match == T5_KOREADER_MATCH_BINARY);

    run(SCENARIO_KEYBOARD_REQUEST);
    assert(keyboard_requests == 1);
    keyboard_pending = true;
    run(SCENARIO_BACK_ONLY);
    assert(username_saves == 1);
    assert(strcmp(stored_username, "bob") == 0);

    wifi_connected_value = false;
    run(SCENARIO_WIFI_REQUEST);
    assert(wifi_requests == 1);
    assert(auth_calls == 0);
    wifi_pending = true;
    run(SCENARIO_AUTH_DISMISS);
    assert(auth_calls == 1);
    assert(auth_renders == 1);
    assert(auth_end_calls == 1);
    assert(settings_renders >= 1);
    return 0;
}
