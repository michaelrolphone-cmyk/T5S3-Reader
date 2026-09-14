#include "T5AppApi.h"
#include "T5FontApi.h"
#include "T5UiApi.h"

#include <stdint.h>
#include <string.h>

#define MAX_CHOICES 64u

static const t5_app_api_v1 *app;
static const t5_font_api_v1 *fonts;
static const t5_ui_api_v1 *ui;
static t5_font_choice_info_t choices[MAX_CHOICES];
static t5_ui_list_row_t rows[MAX_CHOICES];
static uint32_t choice_count;
static int32_t selected_index;

static void load_choices(void) {
    choice_count = fonts->choice_count();
    if (choice_count > MAX_CHOICES) choice_count = MAX_CHOICES;
    memset(rows, 0, sizeof(rows));
    selected_index = 0;
    for (uint32_t i = 0; i < choice_count; ++i) {
        if (!fonts->choice_info(i, &choices[i])) continue;
        rows[i].title = choices[i].name;
        rows[i].value = choices[i].selected ? "Selected" : "";
        if (choices[i].selected) selected_index = (int32_t)i;
    }
}

static void render(void) {
    const t5_ui_chrome_t chrome = {
        .title = "Font Family",
        .subtitle = "Choose the reader font",
        .status = "",
        .back_label = "Back",
        .confirm_label = "Select",
        .previous_label = "Up",
        .next_label = "Down",
    };
    ui->render_list(&chrome, rows, choice_count, selected_index);
}

static int select_current(void) {
    if (selected_index < 0 || (uint32_t)selected_index >= choice_count) return 0;
    return fonts->select_choice((uint32_t)selected_index) == T5_FONT_OK;
}

void app_main(void) {
    app = t5_app_get_api(T5_APP_ABI_VERSION);
    fonts = t5_font_get_api(T5_FONT_API_VERSION);
    ui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!app || !fonts || !ui || !app->poll || !fonts->choice_count || !fonts->choice_info ||
        !fonts->select_choice || !ui->render_list || !ui->hit_test || !ui->next_index || !ui->previous_index) return;

    load_choices();
    render();
    for (;;) {
        t5_app_input_t input;
        if (!app->poll(&input, 50) || input.exit_requested || (input.buttons & T5_APP_BUTTON_BACK)) break;
        if ((input.buttons & T5_APP_BUTTON_UP) || (input.buttons & T5_APP_BUTTON_LEFT)) {
            selected_index = ui->previous_index(selected_index, choice_count);
            render();
        } else if ((input.buttons & T5_APP_BUTTON_DOWN) || (input.buttons & T5_APP_BUTTON_RIGHT)) {
            selected_index = ui->next_index(selected_index, choice_count);
            render();
        } else if (input.tapped) {
            const int32_t hit = ui->hit_test(input.touch_x, input.touch_y);
            if (hit >= 0 && (uint32_t)hit < choice_count) {
                selected_index = hit;
                if (select_current()) break;
            }
        } else if (input.buttons & T5_APP_BUTTON_CONFIRM) {
            if (select_current()) break;
        }
    }
}
