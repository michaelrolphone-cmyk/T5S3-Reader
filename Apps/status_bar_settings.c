#include "T5AppApi.h"
#include "T5StatusBarApi.h"
#include "T5UiApi.h"

#include <stdint.h>
#include <string.h>

static const t5_app_api_v1 *app;
static const t5_status_bar_api_v1 *statusbar;
static const t5_ui_api_v1 *ui;
static t5_status_bar_item_t items[T5_STATUS_BAR_ITEM_COUNT];
static t5_ui_list_row_t rows[T5_STATUS_BAR_ITEM_COUNT];
static uint32_t count;
static int32_t selected;

static void render(void) {
    count = statusbar->item_count();
    if (count > T5_STATUS_BAR_ITEM_COUNT) count = T5_STATUS_BAR_ITEM_COUNT;
    memset(rows, 0, sizeof(rows));
    for (uint32_t i = 0; i < count; ++i) {
        if (!statusbar->item_get(i, &items[i])) continue;
        rows[i].title = items[i].label;
        rows[i].value = items[i].value;
    }
    if (count == 0) selected = 0;
    else if (selected < 0 || (uint32_t)selected >= count) selected = 0;
    const t5_ui_chrome_t chrome = {
        .title = "Customize Status Bar",
        .subtitle = "Reader status information",
        .status = "",
        .back_label = "Back",
        .confirm_label = "Toggle",
        .previous_label = "Up",
        .next_label = "Down",
    };
    ui->render_list(&chrome, rows, count, selected);
}

void app_main(void) {
    app = t5_app_get_api(T5_APP_ABI_VERSION);
    statusbar = t5_status_bar_get_api(T5_STATUS_BAR_API_VERSION);
    ui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!app || !statusbar || !ui || !app->poll || !statusbar->item_count || !statusbar->item_get ||
        !statusbar->item_activate || !ui->render_list || !ui->hit_test || !ui->next_index || !ui->previous_index) return;

    selected = 0;
    render();
    for (;;) {
        t5_app_input_t input;
        if (!app->poll(&input, 50) || input.exit_requested || (input.buttons & T5_APP_BUTTON_BACK)) break;
        if ((input.buttons & T5_APP_BUTTON_UP) || (input.buttons & T5_APP_BUTTON_LEFT)) {
            selected = ui->previous_index(selected, count);
            render();
            continue;
        }
        if ((input.buttons & T5_APP_BUTTON_DOWN) || (input.buttons & T5_APP_BUTTON_RIGHT)) {
            selected = ui->next_index(selected, count);
            render();
            continue;
        }
        if (input.tapped) {
            const int32_t hit = ui->hit_test(input.touch_x, input.touch_y);
            if (hit >= 0 && (uint32_t)hit < count) {
                selected = hit;
                statusbar->item_activate((uint32_t)selected);
                render();
            }
            continue;
        }
        if (input.buttons & T5_APP_BUTTON_CONFIRM) {
            if (selected >= 0 && (uint32_t)selected < count) statusbar->item_activate((uint32_t)selected);
            render();
        }
    }
}
