#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "T5AppApi.h"
#include "T5SdFirmwareApi.h"
#include "T5UiApi.h"

void app_main(void);

static int polls, installs, restarts, renders;

static bool poll_input(t5_app_input_t *input, uint32_t wait_ms) {
    assert(input && wait_ms == 50);
    memset(input, 0, sizeof(*input));
    ++polls;
    input->buttons = T5_APP_BUTTON_CONFIRM;
    return true;
}

static const t5_app_api_v1 app_api = {
    .abi_version = T5_APP_ABI_VERSION,
    .struct_size = sizeof(t5_app_api_v1),
    .poll = poll_input,
};
const t5_app_api_v1 *t5_app_get_api(uint32_t version) { return version == T5_APP_ABI_VERSION ? &app_api : NULL; }

static bool selected_path(char *buffer, size_t capacity) {
    const char *value = "/firmware/test.bin";
    assert(capacity > strlen(value));
    strcpy(buffer, value);
    return true;
}
static size_t image_size(void) { return 100; }
static size_t written_size(void) { return installs ? 100 : 0; }
static t5_sd_firmware_result_t validate(void) { return T5_SD_FIRMWARE_OK; }
static t5_sd_firmware_result_t install(t5_sd_firmware_progress_callback_t callback, void *ctx) {
    ++installs;
    if (callback) callback(ctx);
    return T5_SD_FIRMWARE_OK;
}
static void restart_after_update(void) { ++restarts; }

static const t5_sd_firmware_api_v1 fw_api = {
    .api_version = T5_SD_FIRMWARE_API_VERSION,
    .struct_size = sizeof(t5_sd_firmware_api_v1),
    .selected_path = selected_path,
    .image_size = image_size,
    .written_size = written_size,
    .validate = validate,
    .install = install,
    .restart_after_update = restart_after_update,
};
const t5_sd_firmware_api_v1 *t5_sd_firmware_get_api(uint32_t version) {
    return version == T5_SD_FIRMWARE_API_VERSION ? &fw_api : NULL;
}

static void render_list(const t5_ui_chrome_t *chrome, const t5_ui_list_row_t *rows,
                        uint32_t row_count, int32_t selected_index) {
    assert(chrome && rows && row_count == 1 && selected_index == 0);
    assert(strcmp(chrome->title, "SD Firmware Update") == 0);
    ++renders;
}
static int32_t hit_test(int16_t x, int16_t y) { (void)x; (void)y; return 0; }
static const t5_ui_api_v1 ui_api = {
    .api_version = T5_UI_API_VERSION,
    .struct_size = sizeof(t5_ui_api_v1),
    .render_list = render_list,
    .hit_test = hit_test,
};
const t5_ui_api_v1 *t5_ui_get_api(uint32_t version) { return version == T5_UI_API_VERSION ? &ui_api : NULL; }

int main(void) {
    app_main();
    assert(polls == 1);
    assert(installs == 1);
    assert(restarts == 1);
    assert(renders >= 4);
    return 0;
}
