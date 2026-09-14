#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "T5AppApi.h"
#include "T5CacheApi.h"
#include "T5UiApi.h"

void app_main(void);

static int scenario;
static int polls;
static int clears;
static int warning_renders;
static int result_renders;

static bool app_poll(t5_app_input_t *input, uint32_t wait_ms) {
    assert(input && wait_ms == 50);
    memset(input, 0, sizeof(*input));
    ++polls;
    if (scenario == 0) {
        input->buttons = T5_APP_BUTTON_BACK;
    } else if (polls == 1) {
        input->buttons = T5_APP_BUTTON_CONFIRM;
    } else {
        input->buttons = T5_APP_BUTTON_BACK;
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

static bool clear_reading_cache(t5_cache_clear_result_t *out) {
    assert(out);
    memset(out, 0, sizeof(*out));
    out->directory_available = 1;
    out->removed_count = 3;
    out->failed_count = 1;
    ++clears;
    return true;
}

static const t5_cache_api_v1 cache_api = {
    .api_version = T5_CACHE_API_VERSION,
    .struct_size = sizeof(t5_cache_api_v1),
    .clear_reading_cache = clear_reading_cache,
};

const t5_cache_api_v1 *t5_cache_get_api(uint32_t version) {
    return version == T5_CACHE_API_VERSION ? &cache_api : NULL;
}

static void render_list(const t5_ui_chrome_t *chrome, const t5_ui_list_row_t *rows,
                        uint32_t row_count, int32_t selected_index) {
    assert(chrome && rows && row_count == 1 && selected_index == 0);
    assert(strcmp(chrome->title, "Clear Reading Cache") == 0);
    if (strcmp(rows[0].title, "Clear cached reading data?") == 0) {
        assert(strcmp(chrome->confirm_label, "Clear") == 0);
        ++warning_renders;
    } else {
        assert(strcmp(rows[0].title, "Result") == 0);
        assert(strcmp(rows[0].value, "Completed with errors") == 0);
        assert(strcmp(chrome->status, "3 removed | 1 failed") == 0);
        ++result_renders;
    }
}

static int32_t hit_test(int16_t x, int16_t y) {
    (void)x;
    (void)y;
    return T5_UI_HIT_NONE;
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
    scenario = 0;
    polls = 0;
    app_main();
    assert(clears == 0);
    assert(warning_renders == 1);

    scenario = 1;
    polls = 0;
    app_main();
    assert(clears == 1);
    assert(warning_renders == 2);
    assert(result_renders == 1);
    return 0;
}
