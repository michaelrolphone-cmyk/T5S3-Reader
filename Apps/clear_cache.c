#include "T5AppApi.h"
#include "T5CacheApi.h"
#include "T5UiApi.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const t5_app_api_v1 *app;
static const t5_cache_api_v1 *cache;
static const t5_ui_api_v1 *ui;
static char status_text[128];

typedef enum {
    SCREEN_WARNING = 0,
    SCREEN_RESULT = 1,
} screen_t;

static screen_t screen;

static void render_warning(void) {
    const t5_ui_chrome_t chrome = {
        .title = "Clear Reading Cache",
        .subtitle = "This removes generated reading cache data.",
        .status = "Book files and settings are not deleted.",
        .back_label = "Cancel",
        .confirm_label = "Clear",
        .previous_label = "",
        .next_label = "",
    };
    const t5_ui_list_row_t row = {
        .title = "Clear cached reading data?",
        .subtitle = "Cached EPUB/XTC render data will be regenerated when needed.",
        .value = "Clear",
        .flags = T5_UI_LIST_HIGHLIGHT_VALUE,
    };
    ui->render_list(&chrome, &row, 1, 0);
}

static void render_result(const t5_cache_clear_result_t *result, bool api_ok) {
    const char *value;
    if (!api_ok) {
        value = "Failed";
        snprintf(status_text, sizeof(status_text), "Cache service unavailable");
    } else if (!result->directory_available) {
        value = "Nothing to clear";
        snprintf(status_text, sizeof(status_text), "Reading cache directory was not found");
    } else if (result->failed_count > 0) {
        value = "Completed with errors";
        snprintf(status_text, sizeof(status_text), "%lu removed | %lu failed",
                 (unsigned long)result->removed_count, (unsigned long)result->failed_count);
    } else {
        value = "Cache cleared";
        snprintf(status_text, sizeof(status_text), "%lu item%s removed",
                 (unsigned long)result->removed_count, result->removed_count == 1 ? "" : "s");
    }

    const t5_ui_chrome_t chrome = {
        .title = "Clear Reading Cache",
        .subtitle = "Reading cache cleanup complete",
        .status = status_text,
        .back_label = "Back",
        .confirm_label = "OK",
        .previous_label = "",
        .next_label = "",
    };
    const t5_ui_list_row_t row = {
        .title = "Result",
        .subtitle = NULL,
        .value = value,
        .flags = T5_UI_LIST_HIGHLIGHT_VALUE,
    };
    ui->render_list(&chrome, &row, 1, 0);
}

static void clear_now(void) {
    t5_cache_clear_result_t result;
    memset(&result, 0, sizeof(result));
    const bool ok = cache->clear_reading_cache(&result);
    screen = SCREEN_RESULT;
    render_result(&result, ok);
}

void app_main(void) {
    app = t5_app_get_api(T5_APP_ABI_VERSION);
    cache = t5_cache_get_api(T5_CACHE_API_VERSION);
    ui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!app || !cache || !ui || !app->poll || !cache->clear_reading_cache || !ui->render_list || !ui->hit_test)
        return;

    screen = SCREEN_WARNING;
    status_text[0] = 0;
    render_warning();

    for (;;) {
        t5_app_input_t input;
        if (!app->poll(&input, 50) || input.exit_requested) break;

        if (screen == SCREEN_WARNING) {
            if (input.buttons & T5_APP_BUTTON_BACK) break;
            if (input.buttons & T5_APP_BUTTON_CONFIRM) {
                clear_now();
                continue;
            }
            if (input.tapped) {
                const int32_t hit = ui->hit_test(input.touch_x, input.touch_y);
                if (hit >= 0) clear_now();
            }
            continue;
        }

        if ((input.buttons & (T5_APP_BUTTON_BACK | T5_APP_BUTTON_CONFIRM)) || input.tapped) break;
    }
}
