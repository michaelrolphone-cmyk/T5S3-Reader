#include "T5AppApi.h"

// The ELF owns all grid geometry, selection, pagination and input handling.
// Firmware owns only SD discovery, shared drawing primitives and launch handoff.
static const t5_app_api_v1 *api;
static uint32_t selected, count;
static int columns, rows, page_size, cell_w, cell_h;
static bool missing_icons;

static void layout(void) {
    int w = api->screen_width(), h = api->screen_height();
    columns = w >= 700 ? 4 : 3;
    rows = (h - 180) / 150;
    if (rows < 1) rows = 1;
    cell_w = (w - 32) / columns;
    cell_h = (h - 180) / rows;
    page_size = columns * rows;
}
static void draw(const char *status) {
    api->clear();
    api->draw_text(24, 24, "Apps");
    const uint32_t first = count ? (selected / page_size) * page_size : 0;
    missing_icons = false;
    if (!count) {
        api->draw_text(24, 120, "No installed apps found.");
        api->draw_text(24, 160, "Copy .elf + .json pairs into /Apps on SD.");
    }
    for (int cell = 0; cell < page_size && first + cell < count; ++cell) {
        t5_app_manifest_t app;
        if (!api->installed_apps_get(first + cell, &app)) continue;
        const int x = 16 + (cell % columns) * cell_w;
        const int y = 80 + (cell / columns) * cell_h;
        const int box = 70;
        const int bx = x + (cell_w - box) / 2;
        api->fill_rect(bx, y + 8, box, box, true);
        api->fill_rect(bx + 3, y + 11, box - 6, box - 6, false);
        if (!api->draw_icon(bx + 17, y + 23, app.icon, 18, true)) missing_icons = true;
        api->draw_label(x + 6, y + 86, cell_w - 12, app.display_name);
        if (!app.compatible) api->draw_label(x + 6, y + 115, cell_w - 12, "Update firmware");
        if (first + cell == selected) api->fill_rect(x + 12, y + cell_h - 8, cell_w - 24, 3, true);
    }
    const int bottom = api->screen_height() - 88;
    api->draw_label(8, bottom, api->screen_width() - 16,
                    status ? status : missing_icons ? "Install FAClassic fonts for icons" : "Back: Home   Select: Open");
    api->draw_label(0, bottom + 40, api->screen_width() / 2, "< Previous");
    api->draw_label(api->screen_width() / 2, bottom + 40, api->screen_width() / 2, "Next >");
    api->present(false);
}
static bool launch(void) {
    t5_app_manifest_t app;
    if (!count || !api->installed_apps_get(selected, &app)) return false;
    if (!app.compatible) { draw("This app requires newer firmware"); return false; }
    if (api->request_app_launch(selected)) return true;
    draw("Unable to launch application");
    return false;
}
__attribute__((visibility("default"))) void app_main(void) {
    api = t5_app_get_api(T5_APP_ABI_VERSION);
    if (!api || api->struct_size < sizeof(*api)) return;
    api->installed_apps_refresh();
    count = api->installed_apps_count();
    selected = 0;
    layout();
    draw(0);
    t5_app_input_t input;
    uint32_t previous_buttons = 0;
    while (api->poll(&input, 20)) {
        if (input.exit_requested) return;
        const uint32_t pressed = input.buttons & ~previous_buttons;
        previous_buttons = input.buttons;
        uint32_t old = selected;
        if (count) {
            if (pressed & T5_APP_BUTTON_LEFT) selected = selected ? selected - 1 : count - 1;
            if (pressed & T5_APP_BUTTON_RIGHT) selected = (selected + 1) % count;
            if (pressed & T5_APP_BUTTON_UP) selected = selected >= (uint32_t)columns ? selected - columns : 0;
            if (pressed & T5_APP_BUTTON_DOWN) selected = selected + columns < count ? selected + columns : count - 1;
            if ((pressed & T5_APP_BUTTON_CONFIRM) && launch()) return;
        }
        if (input.tapped && count) {
            const int x = input.touch_x, y = input.touch_y;
            if (y >= api->screen_height() - 48) {
                const uint32_t page = selected / page_size;
                const uint32_t pages = (count + page_size - 1) / page_size;
                selected = ((page + (x < api->screen_width() / 2 ? pages - 1 : 1)) % pages) * page_size;
            } else if (x >= 16 && x < 16 + columns * cell_w && y >= 80 && y < 80 + rows * cell_h) {
                const uint32_t tapped = (selected / page_size) * page_size +
                    ((y - 80) / cell_h) * columns + (x - 16) / cell_w;
                if (tapped < count) { selected = tapped; if (launch()) return; }
            }
        }
        if (selected != old) draw(0);
    }
}
