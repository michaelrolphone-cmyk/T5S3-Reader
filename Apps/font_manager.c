#include "T5AppApi.h"
#include "T5FontApi.h"
#include "T5UiApi.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_FAMILIES 64u
#define VALUE_SIZE 48u

static const t5_app_api_v1 *app;
static const t5_font_api_v1 *fonts;
static const t5_ui_api_v1 *ui;
static t5_font_family_info_t families[MAX_FAMILIES];
static t5_ui_list_row_t rows[MAX_FAMILIES];
static char values[MAX_FAMILIES][VALUE_SIZE];
static uint32_t family_count;
static int32_t selected_index;
static int32_t pending_delete = -1;
static char status_text[128];

static const char *result_text(t5_font_result_t result) {
    switch (result) {
        case T5_FONT_OK: return "Done";
        case T5_FONT_NETWORK_ERROR: return "Network error";
        case T5_FONT_MANIFEST_ERROR: return "Invalid font catalog";
        case T5_FONT_STORAGE_ERROR: return "Storage error";
        case T5_FONT_CHECKSUM_ERROR: return "Checksum error";
        case T5_FONT_INVALID_FILE: return "Invalid font file";
        case T5_FONT_INVALID_INDEX: return "Invalid font selection";
        default: return "Font service unavailable";
    }
}

static void load_rows(void) {
    family_count = fonts->family_count();
    if (family_count > MAX_FAMILIES) family_count = MAX_FAMILIES;
    memset(rows, 0, sizeof(rows));
    memset(values, 0, sizeof(values));
    for (uint32_t i = 0; i < family_count; ++i) {
        if (!fonts->family_info(i, &families[i])) continue;
        rows[i].title = families[i].name;
        rows[i].subtitle = families[i].description;
        if (families[i].has_update) {
            snprintf(values[i], sizeof(values[i]), "Update available");
            rows[i].flags = T5_UI_LIST_HIGHLIGHT_VALUE;
        } else if (families[i].installed) {
            snprintf(values[i], sizeof(values[i]), "Installed");
        } else {
            const unsigned kb = (unsigned)((families[i].total_size + 1023u) / 1024u);
            snprintf(values[i], sizeof(values[i]), "%u KB", kb);
        }
        rows[i].value = values[i];
    }
    if (family_count == 0) selected_index = 0;
    else if (selected_index < 0 || (uint32_t)selected_index >= family_count) selected_index = 0;
}

static void render(void) {
    load_rows();
    const t5_ui_chrome_t chrome = {
        .title = "Manage Fonts",
        .subtitle = "Install, update, or remove SD fonts",
        .status = status_text,
        .back_label = "Back",
        .confirm_label = pending_delete >= 0 ? "Remove" : "Select",
        .previous_label = "Up",
        .next_label = "Down",
    };
    ui->render_list(&chrome, rows, family_count, selected_index);
}

static void progress_cb(const char *family_name, uint32_t file_index, uint32_t file_count,
                        size_t downloaded, size_t total, void *ctx) {
    (void)ctx;
    unsigned pct = total ? (unsigned)((downloaded * 100u) / total) : 0u;
    snprintf(status_text, sizeof(status_text), "%s  %u/%u  %u%%", family_name ? family_name : "Font",
             (unsigned)(file_index + 1u), (unsigned)file_count, pct);
    render();
}

static void activate_selected(void) {
    if (selected_index < 0 || (uint32_t)selected_index >= family_count) return;
    const uint32_t index = (uint32_t)selected_index;

    if (pending_delete == selected_index) {
        const t5_font_result_t result = fonts->delete_family(index);
        snprintf(status_text, sizeof(status_text), "%s", result_text(result));
        pending_delete = -1;
        render();
        return;
    }

    pending_delete = -1;
    if (families[index].installed && !families[index].has_update) {
        pending_delete = selected_index;
        snprintf(status_text, sizeof(status_text), "Press Remove again to delete %s", families[index].name);
        render();
        return;
    }

    snprintf(status_text, sizeof(status_text), "Installing %s...", families[index].name);
    render();
    const t5_font_result_t result = fonts->install_family(index, progress_cb, NULL);
    snprintf(status_text, sizeof(status_text), "%s", result_text(result));
    render();
}

void app_main(void) {
    app = t5_app_get_api(T5_APP_ABI_VERSION);
    fonts = t5_font_get_api(T5_FONT_API_VERSION);
    ui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!app || !fonts || !ui || !app->poll || !fonts->refresh_catalog || !fonts->family_count ||
        !fonts->family_info || !fonts->install_family || !fonts->delete_family || !ui->render_list ||
        !ui->hit_test || !ui->next_index || !ui->previous_index) return;

    selected_index = 0;
    pending_delete = -1;
    status_text[0] = 0;
    const t5_font_result_t refresh = fonts->refresh_catalog();
    if (refresh != T5_FONT_OK) snprintf(status_text, sizeof(status_text), "%s", result_text(refresh));
    render();

    for (;;) {
        t5_app_input_t input;
        if (!app->poll(&input, 50) || input.exit_requested || (input.buttons & T5_APP_BUTTON_BACK)) break;
        if ((input.buttons & T5_APP_BUTTON_UP) || (input.buttons & T5_APP_BUTTON_LEFT)) {
            pending_delete = -1;
            selected_index = ui->previous_index(selected_index, family_count);
            render();
            continue;
        }
        if ((input.buttons & T5_APP_BUTTON_DOWN) || (input.buttons & T5_APP_BUTTON_RIGHT)) {
            pending_delete = -1;
            selected_index = ui->next_index(selected_index, family_count);
            render();
            continue;
        }
        if (input.tapped) {
            const int32_t hit = ui->hit_test(input.touch_x, input.touch_y);
            if (hit >= 0 && (uint32_t)hit < family_count) {
                if (selected_index != hit) pending_delete = -1;
                selected_index = hit;
                activate_selected();
            }
            continue;
        }
        if (input.buttons & T5_APP_BUTTON_CONFIRM) activate_selected();
    }
}
