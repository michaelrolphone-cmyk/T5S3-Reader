#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "T5AppApi.h"

void app_main(void);

static int poll_index;
static int renders;
static int activates;
static int back_exit_calls;

static void set_back_exits_app(bool enabled) {
    assert(!enabled);
    ++back_exit_calls;
}

static uint32_t settings_category_count(void) { return 4; }
static bool settings_category_get(uint32_t category, char *label, size_t capacity) {
    assert(category < 4 && label && capacity > 0);
    label[0] = 'X';
    if (capacity > 1) label[1] = 0;
    return true;
}
static uint32_t settings_count(uint32_t category) {
    assert(category < 4);
    return 2;
}
static bool settings_get(uint32_t category, uint32_t index, t5_app_setting_t *out) {
    assert(category < 4 && index < 2 && out);
    memset(out, 0, sizeof(*out));
    return true;
}
static uint8_t settings_activate(uint32_t category, uint32_t index) {
    assert(category == 0 && index == 0);
    ++activates;
    return T5_APP_SETTING_ACTION_REQUESTED;
}
static void settings_render(uint32_t category, int32_t selected) {
    assert(category == 0);
    if (renders == 0) assert(selected == 0);
    if (renders == 1) assert(selected == 1);
    ++renders;
}
static uint8_t settings_touch(int16_t x, int16_t y, uint32_t *category, int32_t *selected) {
    (void)x; (void)y; (void)category; (void)selected;
    return T5_APP_SETTING_NO_CHANGE;
}

static bool poll_input(t5_app_input_t *input, uint32_t wait_ms) {
    assert(input && wait_ms == 20);
    memset(input, 0, sizeof(*input));
    switch (poll_index++) {
        case 0: return true;
        case 1: input->buttons = T5_APP_BUTTON_DOWN; return true;
        case 2: return true;
        case 3: input->buttons = T5_APP_BUTTON_CONFIRM; return true;
        default: return false;
    }
}

static const t5_app_api_v1 api = {
    .abi_version = T5_APP_ABI_VERSION,
    .struct_size = sizeof(t5_app_api_v1),
    .poll = poll_input,
    .set_back_exits_app = set_back_exits_app,
    .settings_category_count = settings_category_count,
    .settings_category_get = settings_category_get,
    .settings_count = settings_count,
    .settings_get = settings_get,
    .settings_activate = settings_activate,
    .settings_render = settings_render,
    .settings_touch = settings_touch,
};

const t5_app_api_v1 *t5_app_get_api(uint32_t version) {
    return version == T5_APP_ABI_VERSION ? &api : NULL;
}

int main(void) {
    app_main();
    assert(back_exit_calls == 1);
    assert(renders == 2);
    assert(activates == 1);
    assert(poll_index == 4);
    return 0;
}
