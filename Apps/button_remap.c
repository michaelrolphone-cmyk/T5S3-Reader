#include "T5AppApi.h"
#include "T5ButtonRemapApi.h"
#include "T5UiApi.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define UNASSIGNED 0xFFu

static const t5_app_api_v1 *app;
static const t5_button_remap_api_v1 *remap;
static const t5_ui_api_v1 *ui;
static t5_button_remap_mapping_t original;
static t5_button_remap_mapping_t pending;
static t5_ui_list_row_t rows[T5_BUTTON_REMAP_ROLE_COUNT];
static char values[T5_BUTTON_REMAP_ROLE_COUNT][24];
static char status[96];
static int32_t current_step;

static const char *role_name(uint32_t role) {
    static const char *names[T5_BUTTON_REMAP_ROLE_COUNT] = {"Back", "Confirm", "Left", "Right"};
    return role < T5_BUTTON_REMAP_ROLE_COUNT ? names[role] : "?";
}

static void render(void) {
    memset(rows, 0, sizeof(rows));
    for (uint32_t i = 0; i < T5_BUTTON_REMAP_ROLE_COUNT; ++i) {
        rows[i].title = role_name(i);
        if (pending.role_to_hardware[i] == UNASSIGNED) {
            snprintf(values[i], sizeof(values[i]), "Unassigned");
        } else {
            snprintf(values[i], sizeof(values[i]), "Button %u", (unsigned)pending.role_to_hardware[i] + 1u);
        }
        rows[i].value = values[i];
    }
    const t5_ui_chrome_t chrome = {
        .title = "Remap Front Buttons",
        .subtitle = "Press a front button for the selected role",
        .status = status,
        .back_label = "",
        .confirm_label = "",
        .previous_label = "Reset",
        .next_label = "Cancel",
    };
    ui->render_list(&chrome, rows, T5_BUTTON_REMAP_ROLE_COUNT, current_step);
}

static int physical_from_input(uint32_t buttons) {
    if (buttons & T5_APP_BUTTON_BACK) return original.role_to_hardware[T5_BUTTON_REMAP_BACK];
    if (buttons & T5_APP_BUTTON_CONFIRM) return original.role_to_hardware[T5_BUTTON_REMAP_CONFIRM];
    if (buttons & T5_APP_BUTTON_LEFT) return original.role_to_hardware[T5_BUTTON_REMAP_LEFT];
    if (buttons & T5_APP_BUTTON_RIGHT) return original.role_to_hardware[T5_BUTTON_REMAP_RIGHT];
    return -1;
}

static bool already_used(uint8_t hardware) {
    for (uint32_t i = 0; i < T5_BUTTON_REMAP_ROLE_COUNT; ++i) {
        if ((int32_t)i != current_step && pending.role_to_hardware[i] == hardware) return true;
    }
    return false;
}

void app_main(void) {
    app = t5_app_get_api(T5_APP_ABI_VERSION);
    remap = t5_button_remap_get_api(T5_BUTTON_REMAP_API_VERSION);
    ui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!app || !remap || !ui || !app->poll || !app->set_back_exits_app || !remap->read_mapping ||
        !remap->apply_mapping || !remap->reset_defaults || !ui->render_list || !ui->hit_test) return;
    if (!remap->read_mapping(&original)) return;

    app->set_back_exits_app(false);
    memset(&pending, UNASSIGNED, sizeof(pending));
    status[0] = 0;
    current_step = 0;
    render();

    for (;;) {
        t5_app_input_t input;
        if (!app->poll(&input, 50) || input.exit_requested) break;

        if (input.buttons & T5_APP_BUTTON_UP) {
            snprintf(status, sizeof(status), remap->reset_defaults() ? "Default mapping restored" : "Could not save defaults");
            render();
            break;
        }
        if (input.buttons & T5_APP_BUTTON_DOWN) break;

        if (input.tapped) {
            const int32_t hit = ui->hit_test(input.touch_x, input.touch_y);
            if (hit >= 0 && hit < (int32_t)T5_BUTTON_REMAP_ROLE_COUNT) {
                current_step = hit;
                status[0] = 0;
                render();
            }
            continue;
        }

        const int physical = physical_from_input(input.buttons);
        if (physical < 0) continue;
        if (already_used((uint8_t)physical)) {
            snprintf(status, sizeof(status), "Button %u is already assigned", (unsigned)physical + 1u);
            render();
            continue;
        }

        pending.role_to_hardware[current_step] = (uint8_t)physical;
        status[0] = 0;
        ++current_step;
        if (current_step >= (int32_t)T5_BUTTON_REMAP_ROLE_COUNT) {
            if (!remap->apply_mapping(&pending)) {
                current_step = T5_BUTTON_REMAP_ROLE_COUNT - 1;
                snprintf(status, sizeof(status), "Could not save button mapping");
                render();
                continue;
            }
            break;
        }
        render();
    }

    app->set_back_exits_app(true);
}
