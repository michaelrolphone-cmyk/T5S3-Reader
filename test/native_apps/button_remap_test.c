#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "T5AppApi.h"
#include "T5ButtonRemapApi.h"
#include "T5UiApi.h"

void app_main(void);

static int polls;
static int renders;
static int back_mode_changes;
static t5_button_remap_mapping_t applied;
static bool applied_ok;

static bool app_poll(t5_app_input_t *input, uint32_t wait_ms) {
    assert(input && wait_ms == 50);
    memset(input, 0, sizeof(*input));
    ++polls;
    if (polls == 1) input->buttons = T5_APP_BUTTON_CONFIRM;
    else if (polls == 2) input->buttons = T5_APP_BUTTON_BACK;
    else if (polls == 3) input->buttons = T5_APP_BUTTON_RIGHT;
    else input->buttons = T5_APP_BUTTON_LEFT;
    return true;
}

static void set_back_exits(bool enabled) {
    ++back_mode_changes;
    if (back_mode_changes == 1) assert(!enabled);
    else assert(enabled);
}

static const t5_app_api_v1 app_api = {
    .abi_version = T5_APP_ABI_VERSION,
    .struct_size = sizeof(t5_app_api_v1),
    .poll = app_poll,
    .set_back_exits_app = set_back_exits,
};
const t5_app_api_v1 *t5_app_get_api(uint32_t version) {
    return version == T5_APP_ABI_VERSION ? &app_api : NULL;
}

static bool read_mapping(t5_button_remap_mapping_t *out) {
    assert(out);
    out->role_to_hardware[0] = 0;
    out->role_to_hardware[1] = 1;
    out->role_to_hardware[2] = 2;
    out->role_to_hardware[3] = 3;
    return true;
}
static bool apply_mapping(const t5_button_remap_mapping_t *mapping) {
    assert(mapping);
    applied = *mapping;
    applied_ok = true;
    return true;
}
static bool reset_defaults(void) { assert(false); return false; }
static const t5_button_remap_api_v1 remap_api = {
    .api_version = T5_BUTTON_REMAP_API_VERSION,
    .struct_size = sizeof(t5_button_remap_api_v1),
    .read_mapping = read_mapping,
    .apply_mapping = apply_mapping,
    .reset_defaults = reset_defaults,
};
const t5_button_remap_api_v1 *t5_button_remap_get_api(uint32_t version) {
    return version == T5_BUTTON_REMAP_API_VERSION ? &remap_api : NULL;
}

static void render_list(const t5_ui_chrome_t *chrome, const t5_ui_list_row_t *rows,
                        uint32_t count, int32_t selected) {
    assert(chrome && rows && count == 4);
    assert(strcmp(chrome->title, "Remap Front Buttons") == 0);
    assert(selected >= 0 && selected < 4);
    ++renders;
}
static int32_t hit_test(int16_t x, int16_t y) { (void)x; (void)y; return T5_UI_HIT_NONE; }
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
    assert(polls == 4);
    assert(applied_ok);
    assert(applied.role_to_hardware[0] == 1);
    assert(applied.role_to_hardware[1] == 0);
    assert(applied.role_to_hardware[2] == 3);
    assert(applied.role_to_hardware[3] == 2);
    assert(back_mode_changes == 2);
    assert(renders == 4);
    return 0;
}
