#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "T5AppApi.h"
#include "T5NetworkApi.h"
#include "T5StorageApi.h"
#include "T5SystemUiApi.h"
#include "T5UiApi.h"

void app_main(void);

static uint8_t stored_session[20000];
static size_t stored_session_size;
static bool stored_session_available;
static bool back_exits = true;
static bool keyboard_requested;
static bool keyboard_result_available;
static bool navigated_home;
static bool event_sent;
static int phase;
static int http_requests;
static int render_count;
static bool saw_user_turn;
static bool saw_ai_turn;
static bool session_removed;

static int32_t screen_height(void) { return 540; }
static void set_back_exits(bool enabled) { back_exits = enabled; }

static bool storage_exists(const char *path) {
    assert(path != NULL);
    return stored_session_available && strcmp(path, "/sd/.crosspoint/llm_ask.session") == 0;
}

static bool storage_read(const char *path, void *buffer, size_t capacity, size_t *size_out) {
    assert(path != NULL);
    assert(size_out != NULL);
    if (!stored_session_available || strcmp(path, "/sd/.crosspoint/llm_ask.session") != 0) {
        *size_out = 0;
        return false;
    }
    *size_out = stored_session_size;
    if (!buffer || capacity == 0) return true;
    if (capacity < stored_session_size) return false;
    memcpy(buffer, stored_session, stored_session_size);
    return true;
}

static bool storage_write(const char *path, const void *data, size_t size) {
    assert(path != NULL);
    assert(strcmp(path, "/sd/.crosspoint/llm_ask.session") == 0);
    assert(data != NULL);
    assert(size <= sizeof(stored_session));
    memcpy(stored_session, data, size);
    stored_session_size = size;
    stored_session_available = true;
    return true;
}

static bool storage_remove(const char *path) {
    assert(path != NULL);
    assert(strcmp(path, "/sd/.crosspoint/llm_ask.session") == 0);
    stored_session_available = false;
    stored_session_size = 0;
    session_removed = true;
    return true;
}

static bool keyboard_request(const char *title, const char *initial_text, size_t max_length,
                             uint8_t input_type, uint64_t cookie) {
    assert(phase == 1);
    assert(title != NULL && strcmp(title, "Ask Manifold") == 0);
    assert(initial_text != NULL && initial_text[0] == '\0');
    assert(max_length == 280);
    assert(input_type == T5_SYSTEM_KEYBOARD_TEXT);
    assert(cookie == 0x41534B01u);
    keyboard_requested = true;
    return true;
}

static bool keyboard_take_result(char *text, size_t capacity, bool *cancelled, uint64_t *cookie) {
    if (!keyboard_result_available) return false;
    assert(phase == 2);
    assert(text != NULL && capacity > strlen("Hello") + 1);
    strcpy(text, "Hello");
    if (cancelled) *cancelled = false;
    if (cookie) *cookie = 0x41534B01u;
    keyboard_result_available = false;
    return true;
}

static void navigate_home(void) { navigated_home = true; }

static bool wifi_request(uint64_t cookie) {
    (void)cookie;
    assert(!"Wi-Fi picker should not be needed while connected");
    return false;
}

static bool wifi_take_result(bool *connected, bool *cancelled, uint64_t *cookie) {
    (void)connected;
    (void)cancelled;
    (void)cookie;
    return false;
}

static void render_text_view(const t5_ui_chrome_t *chrome, const char *text,
                             int32_t scroll_from_bottom, t5_ui_text_view_result_t *result) {
    assert(chrome != NULL);
    assert(text != NULL);
    assert(strcmp(chrome->title, "Ask") == 0);
    assert(strcmp(chrome->back_label, "Home") == 0);
    assert(strcmp(chrome->previous_label, "Up") == 0);
    assert(strcmp(chrome->next_label, "Down") == 0);
    assert(scroll_from_bottom >= 0);
    ++render_count;
    if (strstr(text, "You: Hello") != NULL) saw_user_turn = true;
    if (strstr(text, "AI: Hi there") != NULL) saw_ai_turn = true;
    if (result) {
        result->max_scroll_lines = 0;
        result->total_lines = 2;
        result->visible_lines = 2;
    }
}

static bool poll_event(t5_ui_event_t *event, uint32_t wait_ms) {
    (void)wait_ms;
    assert(event != NULL);
    if (event_sent) return false;
    event_sent = true;
    event->touch_x = 0;
    event->touch_y = 0;
    event->type = phase == 1 ? T5_UI_EVENT_CONFIRM : T5_UI_EVENT_BACK;
    return true;
}

static bool wifi_connected(void) { return true; }

static bool http_request(const char *url, uint8_t method, const t5_http_header_t *headers,
                         uint32_t header_count, const void *body, size_t body_size,
                         const char *cert_pem, uint32_t timeout_ms, char *response,
                         size_t response_capacity, t5_http_result_t *result) {
    const char *payload = (const char *)body;
    bool saw_content_type = false;
    bool saw_auth = false;
    bool saw_host = false;
    uint32_t i;
    const char reply[] = "{\"choices\":[{\"message\":{\"content\":\"Hi there\"}}]}";

    assert(phase == 2);
    assert(url != NULL && strcmp(url, "https://api.llm7.io/v1/chat/completions") == 0);
    assert(method == T5_HTTP_METHOD_POST);
    assert(headers != NULL && header_count >= 4);
    assert(body != NULL && body_size == strlen(payload));
    assert(strstr(payload, "\"model\":\"fast\"") != NULL);
    assert(strstr(payload, "You are Manifold on a small e-ink reader") != NULL);
    assert(strstr(payload, "\"content\":\"Hello\"") != NULL);
    assert(cert_pem != NULL && strstr(cert_pem, "BEGIN CERTIFICATE") != NULL);
    assert(timeout_ms == 30000);
    assert(response != NULL && response_capacity > sizeof(reply));
    assert(result != NULL);

    for (i = 0; i < header_count; ++i) {
        assert(headers[i].name != NULL && headers[i].value != NULL);
        if (strcmp(headers[i].name, "Content-Type") == 0 && strcmp(headers[i].value, "application/json") == 0)
            saw_content_type = true;
        if (strcmp(headers[i].name, "Authorization") == 0 && strcmp(headers[i].value, "Bearer unused") == 0)
            saw_auth = true;
        if (strcmp(headers[i].name, "Host") == 0 && strcmp(headers[i].value, "api.llm7.io") == 0)
            saw_host = true;
    }
    assert(saw_content_type && saw_auth && saw_host);

    strcpy(response, reply);
    *result = (t5_http_result_t){
        .transport_error = 0,
        .status_code = 200,
        .response_bytes = strlen(reply),
        .flags = 0,
    };
    ++http_requests;
    return true;
}

static const t5_app_api_v1 app_api = {
    .abi_version = T5_APP_ABI_VERSION,
    .struct_size = sizeof(t5_app_api_v1),
    .screen_height = screen_height,
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

static const t5_system_ui_api_v1 system_ui_api = {
    .api_version = T5_SYSTEM_UI_API_VERSION,
    .struct_size = sizeof(t5_system_ui_api_v1),
    .keyboard_request = keyboard_request,
    .keyboard_take_result = keyboard_take_result,
    .navigate_home = navigate_home,
    .wifi_request = wifi_request,
    .wifi_take_result = wifi_take_result,
};

static const t5_ui_api_v1 ui_api = {
    .api_version = T5_UI_API_VERSION,
    .struct_size = sizeof(t5_ui_api_v1),
    .poll_event = poll_event,
    .render_text_view = render_text_view,
};

static const t5_network_api_v1 network_api = {
    .api_version = T5_NETWORK_API_VERSION,
    .struct_size = sizeof(t5_network_api_v1),
    .wifi_connected = wifi_connected,
    .http_request = http_request,
};

const t5_app_api_v1 *t5_app_get_api(uint32_t version) {
    return version == T5_APP_ABI_VERSION ? &app_api : NULL;
}

const t5_storage_api_v1 *t5_storage_get_api(uint32_t version) {
    return version == T5_STORAGE_API_VERSION ? &storage_api : NULL;
}

const t5_system_ui_api_v1 *t5_system_ui_get_api(uint32_t version) {
    return version == T5_SYSTEM_UI_API_VERSION ? &system_ui_api : NULL;
}

const t5_ui_api_v1 *t5_ui_get_api(uint32_t version) {
    return version == T5_UI_API_VERSION ? &ui_api : NULL;
}

const t5_network_api_v1 *t5_network_get_api(uint32_t version) {
    return version == T5_NETWORK_API_VERSION ? &network_api : NULL;
}

int main(void) {
    phase = 1;
    event_sent = false;
    app_main();
    assert(keyboard_requested);
    assert(stored_session_available);
    assert(back_exits);
    assert(!navigated_home);
    assert(http_requests == 0);

    phase = 2;
    event_sent = false;
    keyboard_result_available = true;
    app_main();
    assert(http_requests == 1);
    assert(saw_user_turn);
    assert(saw_ai_turn);
    assert(navigated_home);
    assert(session_removed);
    assert(!stored_session_available);
    assert(back_exits);
    assert(render_count >= 3);
    return 0;
}
