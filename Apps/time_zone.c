#include "T5AppApi.h"
#include "T5TimeZoneApi.h"
#include "T5UiApi.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define MAX_ROWS 96u

static const t5_app_api_v1 *app;
static const t5_time_zone_api_v1 *zones;
static const t5_ui_api_v1 *ui;
static t5_ui_list_row_t rows[MAX_ROWS];
static t5_time_zone_region_info_t region_info[MAX_ROWS];
static t5_time_zone_city_info_t city_info[MAX_ROWS];
static uint32_t row_count;
static uint32_t region;
static int32_t selected;
static bool city_mode;

static void load_rows(void) {
    memset(rows, 0, sizeof(rows));
    row_count = city_mode ? zones->city_count(region) : zones->region_count();
    if (row_count > MAX_ROWS) row_count = MAX_ROWS;
    for (uint32_t i = 0; i < row_count; ++i) {
        if (city_mode) {
            if (!zones->city_info(region, i, &city_info[i])) continue;
            rows[i].title = city_info[i].name;
            rows[i].value = city_info[i].selected ? "Selected" : "";
        } else {
            if (!zones->region_info(i, &region_info[i])) continue;
            rows[i].title = region_info[i].name;
            rows[i].value = region_info[i].selected ? "Selected" : "";
        }
    }
    if (row_count == 0) selected = 0;
    else if (selected < 0 || (uint32_t)selected >= row_count) selected = 0;
}

static void render(void) {
    load_rows();
    const t5_ui_chrome_t chrome = {
        .title = city_mode ? region_info[region].name : "Time Zone",
        .subtitle = city_mode ? "Select a city" : "Select a region",
        .status = "",
        .back_label = "Back",
        .confirm_label = "Select",
        .previous_label = "Up",
        .next_label = "Down",
    };
    ui->render_list(&chrome, rows, row_count, selected);
}

static bool activate(void) {
    if (selected < 0 || (uint32_t)selected >= row_count) return false;
    if (!city_mode) {
        region = (uint32_t)selected;
        city_mode = true;
        selected = 0;
        const uint32_t count = zones->city_count(region);
        for (uint32_t i = 0; i < count && i < MAX_ROWS; ++i) {
            t5_time_zone_city_info_t info;
            if (zones->city_info(region, i, &info) && info.selected) {
                selected = (int32_t)i;
                break;
            }
        }
        render();
        return false;
    }
    return zones->select_city(region, (uint32_t)selected);
}

void app_main(void) {
    app = t5_app_get_api(T5_APP_ABI_VERSION);
    zones = t5_time_zone_get_api(T5_TIME_ZONE_API_VERSION);
    ui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!app || !zones || !ui || !app->poll || !zones->region_count || !zones->region_info ||
        !zones->city_count || !zones->city_info || !zones->select_city || !ui->render_list ||
        !ui->hit_test || !ui->next_index || !ui->previous_index) return;

    city_mode = false;
    region = 0;
    selected = 0;
    const uint32_t regions = zones->region_count();
    for (uint32_t i = 0; i < regions && i < MAX_ROWS; ++i) {
        t5_time_zone_region_info_t info;
        if (zones->region_info(i, &info) && info.selected) {
            region = i;
            selected = (int32_t)i;
            break;
        }
    }
    render();

    for (;;) {
        t5_app_input_t input;
        if (!app->poll(&input, 50) || input.exit_requested) return;
        if (input.buttons & T5_APP_BUTTON_BACK) {
            if (city_mode) {
                city_mode = false;
                selected = (int32_t)region;
                render();
                continue;
            }
            return;
        }
        if ((input.buttons & T5_APP_BUTTON_UP) || (input.buttons & T5_APP_BUTTON_LEFT)) {
            selected = ui->previous_index(selected, row_count);
            render();
            continue;
        }
        if ((input.buttons & T5_APP_BUTTON_DOWN) || (input.buttons & T5_APP_BUTTON_RIGHT)) {
            selected = ui->next_index(selected, row_count);
            render();
            continue;
        }
        if (input.tapped) {
            const int32_t hit = ui->hit_test(input.touch_x, input.touch_y);
            if (hit >= 0 && (uint32_t)hit < row_count) {
                selected = hit;
                if (activate()) return;
            }
            continue;
        }
        if ((input.buttons & T5_APP_BUTTON_CONFIRM) && activate()) return;
    }
}
