#include "T5AppApi.h"
#include "T5NetworkApi.h"
#include "T5SystemUiApi.h"
#include "T5UiApi.h"

#include <stdbool.h>
#include <stdint.h>

#define WIFI_COOKIE 0x5749464900000001ULL

static const t5_app_api_v1 *app;
static const t5_network_api_v1 *network;
static const t5_system_ui_api_v1 *system_ui;
static const t5_ui_api_v1 *ui;

static void render_result(bool connected, bool cancelled) {
    const t5_ui_list_row_t rows[] = {
        {
            .title = "Wi-Fi status",
            .subtitle = cancelled ? "Selection cancelled" : "Network selection complete",
            .value = connected ? "Connected" : "Not connected",
            .flags = connected ? T5_UI_LIST_HIGHLIGHT_VALUE : 0,
        },
        {
            .title = "Choose another network",
            .subtitle = "Open the firmware Wi-Fi selector",
            .value = "",
            .flags = 0,
        },
    };
    const t5_ui_chrome_t chrome = {
        .title = "Wi-Fi Networks",
        .subtitle = "Manage the active wireless connection",
        .status = "",
        .back_label = "Back",
        .confirm_label = "Select",
        .previous_label = "Up",
        .next_label = "Down",
    };
    ui->render_list(&chrome, rows, 2, 0);
}

void app_main(void) {
    app = t5_app_get_api(T5_APP_ABI_VERSION);
    network = t5_network_get_api(T5_NETWORK_API_VERSION);
    system_ui = t5_system_ui_get_api(T5_SYSTEM_UI_API_VERSION);
    ui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!app || !network || !system_ui || !ui || !app->poll || !network->wifi_connected ||
        !system_ui->wifi_request || !system_ui->wifi_take_result || !ui->render_list ||
        !ui->hit_test || !ui->next_index || !ui->previous_index) return;

    bool connected = network->wifi_connected();
    bool cancelled = false;
    uint64_t cookie = 0;
    if (!system_ui->wifi_take_result(&connected, &cancelled, &cookie)) {
        (void)system_ui->wifi_request(WIFI_COOKIE);
        return;
    }

    int32_t selected = 0;
    render_result(connected, cancelled);
    for (;;) {
        t5_app_input_t input;
        if (!app->poll(&input, 50) || input.exit_requested || (input.buttons & T5_APP_BUTTON_BACK)) break;
        if ((input.buttons & T5_APP_BUTTON_UP) || (input.buttons & T5_APP_BUTTON_LEFT)) {
            selected = ui->previous_index(selected, 2);
            if (selected != 0) {
                const t5_ui_list_row_t rows[] = {
                    {.title="Wi-Fi status", .subtitle=cancelled ? "Selection cancelled" : "Network selection complete", .value=connected ? "Connected" : "Not connected", .flags=connected ? T5_UI_LIST_HIGHLIGHT_VALUE : 0},
                    {.title="Choose another network", .subtitle="Open the firmware Wi-Fi selector", .value="", .flags=0},
                };
                const t5_ui_chrome_t chrome = {.title="Wi-Fi Networks", .subtitle="Manage the active wireless connection", .status="", .back_label="Back", .confirm_label="Select", .previous_label="Up", .next_label="Down"};
                ui->render_list(&chrome, rows, 2, selected);
            }
        } else if ((input.buttons & T5_APP_BUTTON_DOWN) || (input.buttons & T5_APP_BUTTON_RIGHT)) {
            selected = ui->next_index(selected, 2);
            const t5_ui_list_row_t rows[] = {
                {.title="Wi-Fi status", .subtitle=cancelled ? "Selection cancelled" : "Network selection complete", .value=connected ? "Connected" : "Not connected", .flags=connected ? T5_UI_LIST_HIGHLIGHT_VALUE : 0},
                {.title="Choose another network", .subtitle="Open the firmware Wi-Fi selector", .value="", .flags=0},
            };
            const t5_ui_chrome_t chrome = {.title="Wi-Fi Networks", .subtitle="Manage the active wireless connection", .status="", .back_label="Back", .confirm_label="Select", .previous_label="Up", .next_label="Down"};
            ui->render_list(&chrome, rows, 2, selected);
        } else if (input.tapped) {
            const int32_t hit = ui->hit_test(input.touch_x, input.touch_y);
            if (hit == 1) {
                (void)system_ui->wifi_request(WIFI_COOKIE);
                break;
            }
        } else if ((input.buttons & T5_APP_BUTTON_CONFIRM) && selected == 1) {
            (void)system_ui->wifi_request(WIFI_COOKIE);
            break;
        }
    }
}
