#include "T5AppApi.h"
#include "T5OtaApi.h"
#include "T5UiApi.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const t5_app_api_v1 *app;
static const t5_ota_api_v1 *ota;
static const t5_ui_api_v1 *ui;
static char latest[T5_OTA_VERSION_MAX];
static char status_text[96];
static char progress_value[32];

static void render_message(const char *title, const char *subtitle, const char *status,
                           const char *value, const char *back_label, const char *confirm_label) {
    const t5_ui_chrome_t chrome = {
        .title = "Firmware Update",
        .subtitle = subtitle,
        .status = status,
        .back_label = back_label,
        .confirm_label = confirm_label,
        .previous_label = "",
        .next_label = "",
    };
    const t5_ui_list_row_t row = {
        .title = title,
        .subtitle = NULL,
        .value = value,
        .flags = T5_UI_LIST_HIGHLIGHT_VALUE,
    };
    ui->render_list(&chrome, &row, 1, 0);
}

static void render_progress(void) {
    const size_t done = ota->processed_size();
    const size_t total = ota->total_size();
    unsigned long percent = 0;
    if (total > 0) percent = (unsigned long)((done * 100u) / total);
    snprintf(progress_value, sizeof(progress_value), "%lu%%", percent);
    snprintf(status_text, sizeof(status_text), "%lu / %lu bytes",
             (unsigned long)done, (unsigned long)total);
    render_message("Installing update", "Downloading and writing firmware", status_text,
                   progress_value, "", "");
}

static void progress_callback(void *ctx) {
    (void)ctx;
    render_progress();
}

static void wait_to_exit(void) {
    for (;;) {
        t5_app_input_t input;
        if (!app->poll(&input, 50) || input.exit_requested || input.tapped ||
            (input.buttons & (T5_APP_BUTTON_BACK | T5_APP_BUTTON_CONFIRM))) return;
    }
}

void app_main(void) {
    app = t5_app_get_api(T5_APP_ABI_VERSION);
    ota = t5_ota_get_api(T5_OTA_API_VERSION);
    ui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!app || !ota || !ui || !app->poll || !ui->render_list || !ota->check_for_update ||
        !ota->is_update_newer || !ota->latest_version || !ota->install_update ||
        !ota->processed_size || !ota->total_size || !ota->restart_after_update) return;

    latest[0] = 0;
    render_message("Checking for updates", "Connected to WiFi", "Contacting update service...",
                   "Checking", "", "");

    const t5_ota_result_t check = ota->check_for_update();
    if (check != T5_OTA_OK) {
        snprintf(status_text, sizeof(status_text), "Update check failed (%u)", (unsigned)check);
        render_message("Update check failed", "Could not check for firmware updates", status_text,
                       "Failed", "Back", "OK");
        wait_to_exit();
        return;
    }

    if (!ota->is_update_newer()) {
        render_message("No update available", "This device is already current", "No newer firmware was found",
                       "Up to date", "Back", "OK");
        wait_to_exit();
        return;
    }

    if (!ota->latest_version(latest, sizeof(latest))) strncpy(latest, "Available", sizeof(latest));
    latest[sizeof(latest) - 1] = 0;
    snprintf(status_text, sizeof(status_text), "Latest version: %s", latest);
    render_message("New firmware available", "Install the available firmware update?", status_text,
                   latest, "Cancel", "Update");

    for (;;) {
        t5_app_input_t input;
        if (!app->poll(&input, 50) || input.exit_requested || (input.buttons & T5_APP_BUTTON_BACK)) return;
        bool confirm = (input.buttons & T5_APP_BUTTON_CONFIRM) != 0;
        if (input.tapped && ui->hit_test) confirm = ui->hit_test(input.touch_x, input.touch_y) >= 0;
        if (!confirm) continue;

        render_progress();
        const t5_ota_result_t installed = ota->install_update(progress_callback, NULL);
        if (installed != T5_OTA_OK) {
            snprintf(status_text, sizeof(status_text), "Firmware install failed (%u)", (unsigned)installed);
            render_message("Update failed", "Firmware was not installed", status_text,
                           "Failed", "Back", "OK");
            wait_to_exit();
            return;
        }

        render_message("Update complete", "Firmware installed successfully", "Restarting device...",
                       latest, "", "");
        for (unsigned i = 0; i < 60; ++i) {
            t5_app_input_t ignored;
            if (!app->poll(&ignored, 50)) break;
        }
        ota->restart_after_update();
        return;
    }
}
