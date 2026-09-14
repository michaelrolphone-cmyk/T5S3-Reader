#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "T5AppApi.h"
#include "T5OtaApi.h"
#include "T5UiApi.h"

void app_main(void);

static int scenario;
static int polls;
static int installs;
static int restarts;
static int progress_renders;
static int available_renders;
static int current_renders;
static size_t processed;
static size_t total;

static bool app_poll(t5_app_input_t *input, uint32_t wait_ms) {
    assert(input && wait_ms == 50);
    memset(input, 0, sizeof(*input));
    ++polls;
    if (scenario == 0) {
        input->buttons = T5_APP_BUTTON_BACK;
    } else if (polls == 1) {
        input->buttons = T5_APP_BUTTON_CONFIRM;
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

static t5_ota_result_t check_for_update(void) { return T5_OTA_OK; }
static bool is_update_newer(void) { return scenario != 0; }
static bool latest_version(char *buffer, size_t size) {
    assert(buffer && size > 0);
    strncpy(buffer, "1.2.0", size - 1);
    buffer[size - 1] = 0;
    return true;
}
static size_t processed_size(void) { return processed; }
static size_t total_size(void) { return total; }
static t5_ota_result_t install_update(t5_ota_progress_callback_t callback, void *ctx) {
    ++installs;
    total = 1000;
    processed = 500;
    callback(ctx);
    processed = 1000;
    callback(ctx);
    return T5_OTA_OK;
}
static void restart_after_update(void) { ++restarts; }

static const t5_ota_api_v1 ota_api = {
    .api_version = T5_OTA_API_VERSION,
    .struct_size = sizeof(t5_ota_api_v1),
    .check_for_update = check_for_update,
    .is_update_newer = is_update_newer,
    .latest_version = latest_version,
    .processed_size = processed_size,
    .total_size = total_size,
    .install_update = install_update,
    .restart_after_update = restart_after_update,
};

const t5_ota_api_v1 *t5_ota_get_api(uint32_t version) {
    return version == T5_OTA_API_VERSION ? &ota_api : NULL;
}

static void render_list(const t5_ui_chrome_t *chrome, const t5_ui_list_row_t *rows,
                        uint32_t row_count, int32_t selected_index) {
    assert(chrome && rows && row_count == 1 && selected_index == 0);
    assert(strcmp(chrome->title, "Firmware Update") == 0);
    if (strcmp(rows[0].title, "New firmware available") == 0) {
        assert(strcmp(rows[0].value, "1.2.0") == 0);
        ++available_renders;
    } else if (strcmp(rows[0].title, "Installing update") == 0) {
        ++progress_renders;
    } else if (strcmp(rows[0].title, "No update available") == 0) {
        ++current_renders;
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
    assert(current_renders == 1);
    assert(installs == 0);
    assert(restarts == 0);

    scenario = 1;
    polls = 0;
    processed = total = 0;
    app_main();
    assert(available_renders == 1);
    assert(installs == 1);
    assert(progress_renders >= 3);
    assert(restarts == 1);
    return 0;
}
