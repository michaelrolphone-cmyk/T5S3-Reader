#include "T5AppApi.h"
#include "T5SystemUiApi.h"
#include "T5UiApi.h"

#include <stdbool.h>
#include <stdint.h>

void app_main(void) {
    const t5_app_api_v1 *app = t5_app_get_api(T5_APP_ABI_VERSION);
    const t5_system_ui_api_v1 *system_ui = t5_system_ui_get_api(T5_SYSTEM_UI_API_VERSION);
    const t5_ui_api_v1 *ui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!app || !system_ui || !ui || !app->poll || !system_ui->file_transfer_request ||
        !ui->render_list || !ui->hit_test) return;

    const t5_ui_list_row_t rows[] = {
        {
            .title = "Start File Transfer",
            .subtitle = "Wi-Fi, Calibre wireless, or reader hotspot",
            .value = "Open",
            .flags = T5_UI_LIST_HIGHLIGHT_VALUE,
        },
    };
    const t5_ui_chrome_t chrome = {
        .title = "File Transfer",
        .subtitle = "Transfer books and files wirelessly",
        .status = "",
        .back_label = "Back",
        .confirm_label = "Open",
        .previous_label = "",
        .next_label = "",
    };

    ui->render_list(&chrome, rows, 1, 0);
    for (;;) {
        t5_app_input_t input;
        if (!app->poll(&input, 50) || input.exit_requested || (input.buttons & T5_APP_BUTTON_BACK)) break;

        bool activate = (input.buttons & T5_APP_BUTTON_CONFIRM) != 0;
        if (input.tapped) activate = ui->hit_test(input.touch_x, input.touch_y) == 0;
        if (activate) {
            (void)system_ui->file_transfer_request();
            break;
        }
    }
}
