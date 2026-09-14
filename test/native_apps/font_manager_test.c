#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "T5AppApi.h"
#include "T5FontApi.h"
#include "T5UiApi.h"

void app_main(void);

static int polls;
static int installs;
static int deletes;
static int renders;

static bool app_poll(t5_app_input_t *input, uint32_t wait_ms) {
    assert(input && wait_ms == 50);
    memset(input, 0, sizeof(*input));
    ++polls;
    if (polls == 1) input->buttons = T5_APP_BUTTON_CONFIRM;
    else if (polls == 2) input->buttons = T5_APP_BUTTON_DOWN;
    else if (polls == 3 || polls == 4) input->buttons = T5_APP_BUTTON_CONFIRM;
    else input->buttons = T5_APP_BUTTON_BACK;
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

static t5_font_result_t refresh_catalog(void) { return T5_FONT_OK; }
static uint32_t family_count(void) { return 2; }
static bool family_info(uint32_t index, t5_font_family_info_t *out) {
    assert(out && index < 2);
    memset(out, 0, sizeof(*out));
    if (index == 0) {
        strcpy(out->name, "Literata");
        strcpy(out->description, "Reader serif");
        out->total_size = 4096;
        out->installed = installs ? 1 : 0;
    } else {
        strcpy(out->name, "Atkinson");
        strcpy(out->description, "Readable sans");
        out->installed = deletes ? 0 : 1;
    }
    return true;
}
static t5_font_result_t install_family(uint32_t index, t5_font_progress_callback_t callback, void *ctx) {
    assert(index == 0);
    ++installs;
    if (callback) callback("Literata", 0, 1, 2048, 4096, ctx);
    return T5_FONT_OK;
}
static t5_font_result_t delete_family(uint32_t index) {
    assert(index == 1);
    ++deletes;
    return T5_FONT_OK;
}
static const t5_font_api_v1 font_api = {
    .api_version = T5_FONT_API_VERSION,
    .struct_size = sizeof(t5_font_api_v1),
    .refresh_catalog = refresh_catalog,
    .family_count = family_count,
    .family_info = family_info,
    .install_family = install_family,
    .delete_family = delete_family,
};
const t5_font_api_v1 *t5_font_get_api(uint32_t version) {
    return version == T5_FONT_API_VERSION ? &font_api : NULL;
}

static void render_list(const t5_ui_chrome_t *chrome, const t5_ui_list_row_t *rows,
                        uint32_t row_count, int32_t selected_index) {
    assert(chrome && rows && row_count == 2);
    assert(strcmp(chrome->title, "Manage Fonts") == 0);
    assert(selected_index >= 0 && selected_index < 2);
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
    app_main();
    assert(installs == 1);
    assert(deletes == 1);
    assert(polls == 5);
    assert(renders >= 7);
    return 0;
}
