#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "T5AppApi.h"
#include "T5StatusBarApi.h"
#include "T5UiApi.h"

void app_main(void);

static int polls;
static int activations;
static int renders;

static bool app_poll(t5_app_input_t *input, uint32_t wait_ms) {
    assert(input && wait_ms == 50);
    memset(input, 0, sizeof(*input));
    ++polls;
    if (polls == 1) input->buttons = T5_APP_BUTTON_CONFIRM;
    else if (polls == 2) input->buttons = T5_APP_BUTTON_DOWN;
    else if (polls == 3) input->buttons = T5_APP_BUTTON_CONFIRM;
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

static uint32_t item_count(void) { return 7; }
static bool item_get(uint32_t index, t5_status_bar_item_t *out) {
    assert(out && index < 7);
    memset(out, 0, sizeof(*out));
    static const char *labels[7] = {"Chapter pages", "Book progress", "Progress bar", "Thickness", "Title", "Battery", "Clock"};
    strcpy(out->label, labels[index]);
    strcpy(out->value, (index == 0 && activations > 0) ? "Show" : "Hide");
    return true;
}
static bool item_activate(uint32_t index) {
    assert(index < 7);
    ++activations;
    return true;
}
static const t5_status_bar_api_v1 status_api = {
    .api_version = T5_STATUS_BAR_API_VERSION,
    .struct_size = sizeof(t5_status_bar_api_v1),
    .item_count = item_count,
    .item_get = item_get,
    .item_activate = item_activate,
};
const t5_status_bar_api_v1 *t5_status_bar_get_api(uint32_t version) {
    return version == T5_STATUS_BAR_API_VERSION ? &status_api : NULL;
}

static void render_list(const t5_ui_chrome_t *chrome, const t5_ui_list_row_t *rows,
                        uint32_t row_count, int32_t selected_index) {
    assert(chrome && rows && row_count == 7);
    assert(strcmp(chrome->title, "Customize Status Bar") == 0);
    assert(selected_index >= 0 && selected_index < 7);
    ++renders;
}
static int32_t hit_test(int16_t x, int16_t y) { (void)x; (void)y; return T5_UI_HIT_NONE; }
static int32_t next_index(int32_t current, uint32_t count) { return (current + 1) % (int32_t)count; }
static int32_t previous_index(int32_t current, uint32_t count) { return current <= 0 ? (int32_t)count - 1 : current - 1; }
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
    assert(polls == 4);
    assert(activations == 2);
    assert(renders == 4);
    return 0;
}
