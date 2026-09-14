#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "T5AppApi.h"
#include "T5SystemUiApi.h"
#include "T5UiApi.h"

void app_main(void);

static int requests;
static int renders;
static int polls;

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

static bool file_transfer_request(void) {
    ++requests;
    return true;
}
static const t5_system_ui_api_v1 system_ui_api = {
    .api_version = T5_SYSTEM_UI_API_VERSION,
    .struct_size = sizeof(t5_system_ui_api_v1),
    .file_transfer_request = file_transfer_request,
};
const t5_system_ui_api_v1 *t5_system_ui_get_api(uint32_t version) {
    return version == T5_SYSTEM_UI_API_VERSION ? &system_ui_api : NULL;
}

static void render_list(const t5_ui_chrome_t *chrome, const t5_ui_list_row_t *rows,
                        uint32_t row_count, int32_t selected_index) {
    assert(chrome && rows && row_count == 1 && selected_index == 0);
    assert(strcmp(chrome->title, "File Transfer") == 0);
    assert(strcmp(rows[0].title, "Start File Transfer") == 0);
    ++renders;
}
static int32_t hit_test(int16_t x, int16_t y) {
    (void)x;
    (void)y;
    return 0;
}
static const t5_ui_api_v1 ui_api = {
    .api_version = T5_UI_API_VERSION,
    .struct_size = sizeof(t5_ui_api_v1),
    .render_list = render_list,
    .hit_test = hit_test,
};
const t5_ui_api_v1 *t5_ui_get_api(uint32_t version) {
    return version == T5_UI_API_VERSION ? &ui_api : NULL;
}

int main(void) {
    app_main();
    assert(renders == 1);
    assert(polls == 1);
    assert(requests == 1);
    return 0;
}
