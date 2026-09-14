#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "T5AppApi.h"
#include "T5KOReaderApi.h"
#include "T5NetworkApi.h"
#include "T5SystemUiApi.h"
#include "T5UiApi.h"

void app_main(void);

static int phase;
static int renders;
static int polls;
static int wifi_requests;
static int auth_calls;
static int end_calls;

static bool app_poll(t5_app_input_t *input, uint32_t wait_ms) {
    assert(input && wait_ms == 50);
    memset(input, 0, sizeof(*input));
    ++polls;
    input->buttons = T5_APP_BUTTON_CONFIRM;
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

static bool wifi_connected(void) { return phase != 0; }
static const t5_network_api_v1 network_api = {
    .api_version = T5_NETWORK_API_VERSION,
    .struct_size = sizeof(t5_network_api_v1),
    .wifi_connected = wifi_connected,
};
const t5_network_api_v1 *t5_network_get_api(uint32_t version) {
    return version == T5_NETWORK_API_VERSION ? &network_api : NULL;
}

static bool wifi_request(uint64_t cookie) {
    assert(cookie == 0x4B4F524541555448ULL);
    ++wifi_requests;
    return true;
}
static bool wifi_take_result(bool *connected, bool *cancelled, uint64_t *cookie) {
    if (phase == 0) return false;
    assert(connected && cancelled && cookie);
    *connected = true;
    *cancelled = false;
    *cookie = 0x4B4F524541555448ULL;
    return true;
}
static const t5_system_ui_api_v1 system_ui_api = {
    .api_version = T5_SYSTEM_UI_API_VERSION,
    .struct_size = sizeof(t5_system_ui_api_v1),
    .wifi_request = wifi_request,
    .wifi_take_result = wifi_take_result,
};
const t5_system_ui_api_v1 *t5_system_ui_get_api(uint32_t version) {
    return version == T5_SYSTEM_UI_API_VERSION ? &system_ui_api : NULL;
}

static bool authenticate(t5_koreader_auth_result_t *result) {
    assert(result);
    ++auth_calls;
    memset(result, 0, sizeof(*result));
    result->status = T5_KOREADER_AUTH_OK;
    strcpy(result->message, "Authentication successful");
    return true;
}
static void end_auth_session(void) { ++end_calls; }
static const t5_koreader_api_v1 koreader_api = {
    .api_version = T5_KOREADER_API_VERSION,
    .struct_size = sizeof(t5_koreader_api_v1),
    .authenticate = authenticate,
    .end_auth_session = end_auth_session,
};
const t5_koreader_api_v1 *t5_koreader_get_api(uint32_t version) {
    return version == T5_KOREADER_API_VERSION ? &koreader_api : NULL;
}

static void render_list(const t5_ui_chrome_t *chrome, const t5_ui_list_row_t *rows,
                        uint32_t row_count, int32_t selected_index) {
    assert(chrome && rows && row_count == 1 && selected_index == 0);
    assert(strcmp(chrome->title, "KOReader Authentication") == 0);
    ++renders;
}
static const t5_ui_api_v1 ui_api = {
    .api_version = T5_UI_API_VERSION,
    .struct_size = sizeof(t5_ui_api_v1),
    .render_list = render_list,
};
const t5_ui_api_v1 *t5_ui_get_api(uint32_t version) {
    return version == T5_UI_API_VERSION ? &ui_api : NULL;
}

int main(void) {
    phase = 0;
    app_main();
    assert(wifi_requests == 1);
    assert(auth_calls == 0);
    assert(end_calls == 0);

    phase = 1;
    app_main();
    assert(auth_calls == 1);
    assert(end_calls == 1);
    assert(polls == 1);
    assert(renders >= 5);
    return 0;
}
