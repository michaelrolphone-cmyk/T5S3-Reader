#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "T5AppApi.h"
#include "T5NetworkApi.h"
#include "T5SystemUiApi.h"
#include "T5UiApi.h"

void app_main(void);

static int phase;
static int wifi_requests;
static int renders;
static int polls;

static bool app_poll(t5_app_input_t *input, uint32_t wait_ms) {
    assert(input && wait_ms == 50);
    memset(input, 0, sizeof(*input));
    ++polls;
    input->buttons = T5_APP_BUTTON_BACK;
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
    assert(cookie == 0x5749464900000001ULL);
    ++wifi_requests;
    return true;
}
static bool wifi_take_result(bool *connected, bool *cancelled, uint64_t *cookie) {
    if (phase == 0) return false;
    assert(connected && cancelled && cookie);
    *connected = true;
    *cancelled = false;
    *cookie = 0x5749464900000001ULL;
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

static void render_list(const t5_ui_chrome_t *chrome, const t5_ui_list_row_t *rows,
                        uint32_t row_count, int32_t selected_index) {
    assert(chrome && rows && row_count == 2);
    assert(selected_index >= 0 && selected_index < 2);
    assert(strcmp(chrome->title, "Wi-Fi Networks") == 0);
    assert(strcmp(rows[0].value, "Connected") == 0);
    ++renders;
}
static int32_t hit_test(int16_t x, int16_t y) { (void)x; (void)y; return T5_UI_HIT_NONE; }
static int32_t next_index(int32_t current, uint32_t count) { return count ? (current + 1) % (int32_t)count : 0; }
static int32_t previous_index(int32_t current, uint32_t count) {
    return count ? (current + (int32_t)count - 1) % (int32_t)count : 0;
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
    phase = 0;
    app_main();
    assert(wifi_requests == 1);
    assert(renders == 0);
    assert(polls == 0);

    phase = 1;
    app_main();
    assert(wifi_requests == 1);
    assert(renders == 1);
    assert(polls == 1);
    return 0;
}
