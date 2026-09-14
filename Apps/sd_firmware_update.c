#include "T5AppApi.h"
#include "T5SdFirmwareApi.h"
#include "T5UiApi.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const t5_app_api_v1 *app;
static const t5_sd_firmware_api_v1 *fw;
static const t5_ui_api_v1 *ui;
static char path[T5_SD_FIRMWARE_PATH_MAX];
static char status_text[96];
static unsigned last_percent = 101;

static const char *base_name(const char *value) {
    const char *last = strrchr(value, '/');
    return last ? last + 1 : value;
}

static const char *error_text(t5_sd_firmware_result_t result) {
    switch (result) {
        case T5_SD_FIRMWARE_FILE_OPEN_FAILED: return "Could not open firmware file";
        case T5_SD_FIRMWARE_TOO_LARGE: return "Firmware image is too large";
        case T5_SD_FIRMWARE_TOO_SMALL: return "Firmware image is too small";
        case T5_SD_FIRMWARE_WRITE_FAILED: return "Firmware write failed";
        case T5_SD_FIRMWARE_INVALID: return "Invalid firmware image";
        default: return "Firmware update unavailable";
    }
}

static void render_confirm(void) {
    const t5_ui_chrome_t chrome = {
        .title = "SD Firmware Update",
        .subtitle = "Validated firmware image",
        .status = "Confirm to write firmware",
        .back_label = "Cancel",
        .confirm_label = "Update",
        .previous_label = "",
        .next_label = "",
    };
    const t5_ui_list_row_t row = {
        .title = "Update firmware?",
        .subtitle = base_name(path),
        .value = "Ready",
        .flags = T5_UI_LIST_HIGHLIGHT_VALUE,
    };
    ui->render_list(&chrome, &row, 1, 0);
}

static void render_status(const char *title, const char *value, const char *status) {
    const t5_ui_chrome_t chrome = {
        .title = "SD Firmware Update",
        .subtitle = title,
        .status = status,
        .back_label = "Back",
        .confirm_label = "OK",
        .previous_label = "",
        .next_label = "",
    };
    const t5_ui_list_row_t row = {
        .title = base_name(path),
        .subtitle = NULL,
        .value = value,
        .flags = T5_UI_LIST_HIGHLIGHT_VALUE,
    };
    ui->render_list(&chrome, &row, 1, 0);
}

static void render_progress(void *ctx) {
    (void)ctx;
    const size_t total = fw->image_size();
    const size_t written = fw->written_size();
    const unsigned percent = total ? (unsigned)((written * 100u) / total) : 0u;
    if (percent == last_percent) return;
    last_percent = percent;
    snprintf(status_text, sizeof(status_text), "%u%% | %lu / %lu bytes", percent,
             (unsigned long)written, (unsigned long)total);
    render_status("Updating firmware - do not power off", "Writing", status_text);
}

void app_main(void) {
    app = t5_app_get_api(T5_APP_ABI_VERSION);
    fw = t5_sd_firmware_get_api(T5_SD_FIRMWARE_API_VERSION);
    ui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!app || !fw || !ui || !app->poll || !fw->selected_path || !fw->validate || !fw->install ||
        !fw->restart_after_update || !ui->render_list || !ui->hit_test) return;

    memset(path, 0, sizeof(path));
    if (!fw->selected_path(path, sizeof(path))) return;

    render_status("Validating firmware", "Checking", "Verifying image integrity");
    const t5_sd_firmware_result_t validation = fw->validate();
    if (validation != T5_SD_FIRMWARE_OK) {
        render_status("Update failed", "Invalid", error_text(validation));
        for (;;) {
            t5_app_input_t input;
            if (!app->poll(&input, 50) || input.exit_requested || input.tapped ||
                (input.buttons & (T5_APP_BUTTON_BACK | T5_APP_BUTTON_CONFIRM))) break;
        }
        return;
    }

    render_confirm();
    for (;;) {
        t5_app_input_t input;
        if (!app->poll(&input, 50) || input.exit_requested) return;
        if (input.buttons & T5_APP_BUTTON_BACK) return;
        if (!(input.buttons & T5_APP_BUTTON_CONFIRM) && !input.tapped) continue;
        if (input.tapped && ui->hit_test(input.touch_x, input.touch_y) < 0) continue;
        break;
    }

    last_percent = 101;
    render_status("Updating firmware - do not power off", "Writing", "0%");
    const t5_sd_firmware_result_t result = fw->install(render_progress, NULL);
    if (result != T5_SD_FIRMWARE_OK) {
        render_status("Update failed", "Failed", error_text(result));
        for (;;) {
            t5_app_input_t input;
            if (!app->poll(&input, 50) || input.exit_requested || input.tapped ||
                (input.buttons & (T5_APP_BUTTON_BACK | T5_APP_BUTTON_CONFIRM))) break;
        }
        return;
    }

    render_status("Update complete", "Restarting", "Firmware installed successfully");
    fw->restart_after_update();
}
