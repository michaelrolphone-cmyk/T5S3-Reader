#include "T5AppApi.h"
#include "T5StorageApi.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HOME_APPS_PATH "/sd/Apps/.home_apps"
#define MAX_HOME_APPS 128u
#define HOME_APPS_CAPACITY (MAX_HOME_APPS * 128u)
#define CONFIRM_HOLD_MS 700u

// The ELF owns all grid geometry, selection, pagination and input handling.
// Firmware owns only SD discovery, shared drawing primitives and launch handoff.
static const t5_app_api_v1 *api;
static const t5_storage_api_v1 *storage;
static uint32_t selected, count;
static int columns, rows, page_size, cell_w, cell_h;
static bool missing_icons;
static bool home_pinned[MAX_HOME_APPS];

static bool has_required_app_api(void) {
    const size_t required = offsetof(t5_app_api_v1, draw_label) + sizeof(api->draw_label);
    return api && api->struct_size >= required && api->screen_width && api->screen_height && api->clear &&
           api->draw_text && api->fill_rect && api->present && api->poll && api->millis &&
           api->installed_apps_refresh && api->installed_apps_count && api->installed_apps_get &&
           api->request_app_launch && api->draw_icon && api->draw_label;
}

static bool has_storage_api(void) {
    const size_t required = offsetof(t5_storage_api_v1, write_file_atomic) + sizeof(storage->write_file_atomic);
    return storage && storage->struct_size >= required && storage->exists && storage->read_file &&
           storage->write_file_atomic;
}

static int rounded_inset_for_row(int row, int height, int radius) {
    int edge;
    int x = 0;
    if (radius <= 0 || (row >= radius && row < height - radius)) return 0;
    edge = row < radius ? radius - 1 - row : row - (height - radius);
    while (x < radius && x * x + edge * edge < radius * radius) ++x;
    return radius - x;
}

static void fill_rounded_rect(int x, int y, int width, int height, int radius, bool black) {
    int start;
    int inset;
    if (width <= 0 || height <= 0) return;
    if (radius < 0) radius = 0;
    if (radius > width / 2) radius = width / 2;
    if (radius > height / 2) radius = height / 2;
    if (radius == 0) {
        api->fill_rect(x, y, width, height, black);
        return;
    }

    start = 0;
    inset = rounded_inset_for_row(0, height, radius);
    for (int row = 1; row <= height; ++row) {
        const int next = row < height ? rounded_inset_for_row(row, height, radius) : -1;
        if (next != inset) {
            api->fill_rect(x + inset, y + start, width - inset * 2, row - start, black);
            start = row;
            inset = next;
        }
    }
}

static void load_home_pins(void) {
    memset(home_pinned, 0, sizeof(home_pinned));
    if (!has_storage_api() || !storage->exists(HOME_APPS_PATH)) return;

    size_t size = 0;
    if (!storage->read_file(HOME_APPS_PATH, NULL, 0, &size) || size == 0 || size > HOME_APPS_CAPACITY) return;

    char *data = (char *)malloc(size + 1);
    if (!data) return;
    size_t actual = 0;
    if (!storage->read_file(HOME_APPS_PATH, data, size, &actual) || actual != size) {
        free(data);
        return;
    }
    data[size] = '\0';

    size_t start = 0;
    while (start < size) {
        size_t end = start;
        while (end < size && data[end] != '\n' && data[end] != '\r') ++end;
        if (end > start) {
            const size_t name_len = end - start;
            for (uint32_t i = 0; i < count && i < MAX_HOME_APPS; ++i) {
                t5_app_manifest_t app;
                if (!api->installed_apps_get(i, &app)) continue;
                if (strlen(app.file_name) == name_len && !memcmp(app.file_name, data + start, name_len)) {
                    home_pinned[i] = true;
                    break;
                }
            }
        }
        while (end < size && (data[end] == '\n' || data[end] == '\r')) ++end;
        start = end;
    }
    free(data);
}

static bool save_home_pins(void) {
    if (!has_storage_api()) return false;
    char *payload = (char *)malloc(HOME_APPS_CAPACITY + 1u);
    if (!payload) return false;

    size_t used = 0;
    for (uint32_t i = 0; i < count && i < MAX_HOME_APPS; ++i) {
        if (!home_pinned[i]) continue;
        t5_app_manifest_t app;
        if (!api->installed_apps_get(i, &app)) continue;
        const size_t len = strlen(app.file_name);
        if (used + len + 1u > HOME_APPS_CAPACITY) {
            free(payload);
            return false;
        }
        memcpy(payload + used, app.file_name, len);
        used += len;
        payload[used++] = '\n';
    }

    const bool ok = storage->write_file_atomic(HOME_APPS_PATH, payload, used);
    free(payload);
    return ok;
}

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
        const int border = 3;
        const int radius = 12;
        const int icon_cell = 18;
        const int bx = x + (cell_w - box) / 2;
        const int by = y + 8;
        fill_rounded_rect(bx, by, box, box, radius, true);
        fill_rounded_rect(bx + border, by + border, box - border * 2, box - border * 2,
                          radius - border, false);
        if (!api->draw_icon(bx + (box - icon_cell) / 2, by + (box - icon_cell) / 2,
                            app.icon, icon_cell, true)) {
            missing_icons = true;
        }
        api->draw_label(x + 6, y + 86, cell_w - 12, app.display_name);
        if (!app.compatible) {
            char required[48];
            snprintf(required, sizeof(required), "Needs %s", app.min_firmware_version);
            api->draw_label(x + 6, y + 115, cell_w - 12, required);
        }
        if (first + cell == selected) api->fill_rect(x + 12, y + cell_h - 8, cell_w - 24, 3, true);
    }

    const int bottom = api->screen_height() - 88;
    const int width = api->screen_width();
    api->draw_label(8, bottom, width - 16,
                    status ? status : missing_icons ? "Some Font Awesome icons unavailable" :
                    has_storage_api() ? "Confirm: Open   Hold Confirm: Home" : "Confirm: Open");
    api->draw_label(0, bottom + 40, width / 3, "< Previous");
    api->draw_label(width / 3, bottom + 40, width / 3,
                    count && selected < MAX_HOME_APPS && home_pinned[selected] ? "Remove Home" : "Add Home");
    api->draw_label((width * 2) / 3, bottom + 40, width - (width * 2) / 3, "Next >");
    api->present(false);
}

static bool toggle_home(void) {
    t5_app_manifest_t app;
    if (!count || selected >= MAX_HOME_APPS || !api->installed_apps_get(selected, &app)) return false;
    if (!has_storage_api()) {
        draw("Home pinning unavailable on this firmware");
        return false;
    }
    if (!app.compatible) {
        char status[80];
        snprintf(status, sizeof(status), "Requires firmware %s", app.min_firmware_version);
        draw(status);
        return false;
    }

    const bool old = home_pinned[selected];
    home_pinned[selected] = !old;
    if (!save_home_pins()) {
        home_pinned[selected] = old;
        draw("Unable to save Home apps");
        return false;
    }
    draw(home_pinned[selected] ? "Added to Home" : "Removed from Home");
    return true;
}

static bool launch(void) {
    t5_app_manifest_t app;
    if (!count || !api->installed_apps_get(selected, &app)) return false;
    if (!app.compatible) {
        char status[80];
        snprintf(status, sizeof(status), "Requires firmware %s", app.min_firmware_version);
        draw(status);
        return false;
    }
    if (api->request_app_launch(selected)) return true;
    draw("Unable to queue application launch");
    return false;
}

__attribute__((visibility("default"))) void app_main(void) {
    api = t5_app_get_api(T5_APP_ABI_VERSION);
    storage = t5_storage_get_api(T5_STORAGE_API_VERSION);
    if (!has_required_app_api()) return;
    api->installed_apps_refresh();
    count = api->installed_apps_count();
    if (count > MAX_HOME_APPS) count = MAX_HOME_APPS;
    selected = 0;
    load_home_pins();
    layout();
    draw(0);

    t5_app_input_t input;
    uint32_t previous_buttons = 0;
    uint32_t confirm_started = 0;
    bool confirm_hold_handled = false;

    while (api->poll(&input, 20)) {
        if (input.exit_requested) return;
        const uint32_t pressed = input.buttons & ~previous_buttons;
        const uint32_t released = previous_buttons & ~input.buttons;
        uint32_t old = selected;

        if (count) {
            if (pressed & T5_APP_BUTTON_LEFT) selected = selected ? selected - 1 : count - 1;
            if (pressed & T5_APP_BUTTON_RIGHT) selected = (selected + 1) % count;
            if (pressed & T5_APP_BUTTON_UP) selected = selected >= (uint32_t)columns ? selected - columns : 0;
            if (pressed & T5_APP_BUTTON_DOWN) selected = selected + columns < count ? selected + columns : count - 1;

            if (pressed & T5_APP_BUTTON_CONFIRM) {
                confirm_started = api->millis();
                confirm_hold_handled = false;
            }
            if ((input.buttons & T5_APP_BUTTON_CONFIRM) && !confirm_hold_handled &&
                api->millis() - confirm_started >= CONFIRM_HOLD_MS) {
                toggle_home();
                confirm_hold_handled = true;
            }
            if ((released & T5_APP_BUTTON_CONFIRM) && !confirm_hold_handled && launch()) return;
        }

        if (input.tapped && count) {
            const int x = input.touch_x, y = input.touch_y;
            if (y >= api->screen_height() - 48) {
                const int width = api->screen_width();
                if (x < width / 3) {
                    const uint32_t page = selected / page_size;
                    const uint32_t pages = (count + page_size - 1) / page_size;
                    selected = ((page + pages - 1) % pages) * page_size;
                } else if (x < (width * 2) / 3) {
                    toggle_home();
                } else {
                    const uint32_t page = selected / page_size;
                    const uint32_t pages = (count + page_size - 1) / page_size;
                    selected = ((page + 1) % pages) * page_size;
                }
            } else if (x >= 16 && x < 16 + columns * cell_w && y >= 80 && y < 80 + rows * cell_h) {
                const uint32_t tapped = (selected / page_size) * page_size +
                    ((y - 80) / cell_h) * columns + (x - 16) / cell_w;
                if (tapped < count) {
                    if (tapped == selected) {
                        if (launch()) return;
                    } else {
                        selected = tapped;
                    }
                }
            }
        }

        previous_buttons = input.buttons;
        if (selected != old) draw(0);
    }
}
