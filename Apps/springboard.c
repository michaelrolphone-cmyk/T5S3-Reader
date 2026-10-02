#include "T5AppApi.h"
#include "T5StorageApi.h"
#include "T5VideoApi.h"
#include "T5HardwareTakeover.h"

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
static bool edit_mode;
static bool selection_visible;
static bool home_pinned[MAX_HOME_APPS];

static bool has_required_app_api(void) {
    const size_t required = offsetof(t5_app_api_v1, fill_rounded_rect_tone) +
                            sizeof(api->fill_rounded_rect_tone);
    return api && api->struct_size >= required && api->screen_width && api->screen_height && api->clear &&
           api->draw_text && api->fill_rect && api->present && api->poll && api->millis &&
           api->installed_apps_refresh && api->installed_apps_count && api->installed_apps_get &&
           api->request_app_launch && api->draw_icon && api->draw_label &&
           api->fill_rounded_rect_tone;
}

static bool has_storage_api(void) {
    const size_t required = offsetof(t5_storage_api_v1, write_file_atomic) + sizeof(storage->write_file_atomic);
    return storage && storage->struct_size >= required && storage->exists && storage->read_file &&
           storage->write_file_atomic;
}

static bool filename_matches(const char *name, const char *data, size_t length) {
    if (!name || strlen(name) != length) return false;
    for (size_t i = 0; i < length; ++i) {
        if (name[i] != data[i]) return false;
    }
    return true;
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

static void tone_rounded_rect(int x, int y, int width, int height,
                              int radius, uint8_t tone) {
    api->fill_rounded_rect_tone(x, y, width, height, radius, tone);
}

static void draw_glossy_icon_tile(int x, int y, int size) {
    const int outer_radius = 20;

    // Raised outer frame: dark lip, pale metallic rim, dark bevel, then face.
    tone_rounded_rect(x, y, size, size, outer_radius, T5_APP_TONE_BLACK);
    tone_rounded_rect(x + 1, y + 1, size - 2, size - 2,
                      outer_radius - 1, T5_APP_TONE_LIGHT_GRAY);
    tone_rounded_rect(x + 3, y + 3, size - 6, size - 6,
                      outer_radius - 3, T5_APP_TONE_WHITE);
    tone_rounded_rect(x + 5, y + 5, size - 10, size - 10,
                      outer_radius - 5, T5_APP_TONE_DARK_GRAY);
    tone_rounded_rect(x + 8, y + 8, size - 16, size - 16,
                      outer_radius - 8, T5_APP_TONE_BLACK);

    // Gloss: a broad dark-gray reflection under a small light-gray/white crest.
    // These are actual renderer gray tones rather than sparse hand-made pixels.
    tone_rounded_rect(x + 11, y + 10, size - 22, 18, 9, T5_APP_TONE_DARK_GRAY);
    tone_rounded_rect(x + 14, y + 10, size - 28, 9, 5, T5_APP_TONE_LIGHT_GRAY);
    tone_rounded_rect(x + 20, y + 11, size / 3, 3, 2, T5_APP_TONE_WHITE);

    // Lower bevel and side reflection make the tile read as a raised glossy frame.
    tone_rounded_rect(x + 14, y + size - 12, size - 28, 4, 2, T5_APP_TONE_DARK_GRAY);
    tone_rounded_rect(x + size - 12, y + 20, 4, size / 3, 2, T5_APP_TONE_DARK_GRAY);
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
                if (filename_matches(app.file_name, data + start, name_len)) {
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

static uint32_t page_count(void) {
    if (!count || page_size <= 0) return 1;
    return (count + (uint32_t)page_size - 1u) / (uint32_t)page_size;
}

static uint32_t current_page(void) {
    if (!count || page_size <= 0) return 0;
    return selected / (uint32_t)page_size;
}

static void draw_edit_button(void) {
    const int width = api->screen_width();
    const int button_w = 68;
    const int button_h = 32;
    const int border = 2;
    const int radius = 9;
    const int x = width - button_w - 16;
    const int y = 16;
    fill_rounded_rect(x, y, button_w, button_h, radius, true);
    fill_rounded_rect(x + border, y + border, button_w - border * 2,
                      button_h - border * 2, radius - border, false);
    // UI_12 glyphs sit visually low in this compact control. Pull the text
    // toward the optical center instead of aligning by the font's line box.
    api->draw_label(x, y + 3, button_w, edit_mode ? "DONE" : "EDIT");
}

static void draw_page_dots(void) {
    const uint32_t pages = page_count();
    const uint32_t page = current_page();
    const int spacing = 16;
    const int total_w = (int)pages * spacing - 8;
    const int left = (api->screen_width() - total_w) / 2;
    const int center_y = api->screen_height() - 28;

    for (uint32_t i = 0; i < pages; ++i) {
        const int size = i == page ? 9 : 5;
        const int center_x = left + (int)i * spacing + 4;
        fill_rounded_rect(center_x - size / 2, center_y - size / 2,
                          size, size, size / 2, true);
    }
}

static bool edit_button_hit(int x, int y) {
    const int button_w = 68;
    const int button_h = 32;
    const int left = api->screen_width() - button_w - 16;
    return x >= left && x < left + button_w && y >= 16 && y < 16 + button_h;
}

static bool page_dots_hit(int x, int y) {
    const uint32_t pages = page_count();
    const int spacing = 16;
    const int total_w = (int)pages * spacing - 8;
    const int left = (api->screen_width() - total_w) / 2;
    const int right = left + total_w;
    const int center_y = api->screen_height() - 28;
    return x >= left - 10 && x <= right + 10 &&
           y >= center_y - 14 && y <= center_y + 14;
}

static void change_page(bool forward) {
    const uint32_t pages = page_count();
    const uint32_t next = (current_page() + (forward ? 1u : pages - 1u)) % pages;
    selected = next * (uint32_t)page_size;
    if (count && selected >= count) selected = count - 1u;
    selection_visible = false;
}

static void raster_page(const char *status) {
    api->clear();
    draw_edit_button();
    const uint32_t first = count ? current_page() * (uint32_t)page_size : 0;
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
        const int box = 82;
        const int icon_cell = 18;
        const int bx = x + (cell_w - box) / 2;
        const int by = y + 8;
        if (edit_mode && home_pinned[first + cell]) {
            const int highlight_border = 3;
            const int hx = x + 4;
            const int hy = y + 2;
            const int hw = cell_w - 8;
            const int hh = cell_h - 12;
            fill_rounded_rect(hx, hy, hw, hh, 14, true);
            fill_rounded_rect(hx + highlight_border, hy + highlight_border,
                              hw - highlight_border * 2, hh - highlight_border * 2,
                              14 - highlight_border, false);
        }
        draw_glossy_icon_tile(bx, by, box);
        if (!api->draw_icon(bx + (box - icon_cell) / 2, by + (box - icon_cell) / 2,
                            app.icon, icon_cell, false)) {
            missing_icons = true;
        }
        api->draw_label(x + 6, y + 98, cell_w - 12, app.display_name);
        if (!app.compatible) {
            char required[48];
            snprintf(required, sizeof(required), "Needs %s", app.min_firmware_version);
            api->draw_label(x + 6, y + 115, cell_w - 12, required);
        }
        if (selection_visible && first + cell == selected)
            api->fill_rect(x + 12, y + cell_h - 8, cell_w - 24, 3, true);
    }

    if (status) {
        api->draw_label(8, api->screen_height() - 72, api->screen_width() - 16, status);
    } else if (missing_icons) {
        api->draw_label(8, api->screen_height() - 72, api->screen_width() - 16,
                        "Some Font Awesome icons unavailable");
    }
    draw_page_dots();
}

#include "springboard_video.inc"
static void draw(const char* status) {
    if(sv_enabled) {sv_refresh(status);sv_present();}
    else {raster_page(status);api->present(false);}
}

static bool set_edit_mode(bool enabled) {
    if (enabled && !has_storage_api()) {
        draw("Home editing unavailable on this firmware");
        return false;
    }
    edit_mode = enabled;
    selection_visible = false;
    draw(0);
    return true;
}

static void toggle_edit_mode(void) {
    (void)set_edit_mode(!edit_mode);
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
    draw(0);
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

__attribute__((visibility("default"))) uint32_t app_hardware_takeover(void) {
    const t5_app_api_v1* host=t5_app_get_api(T5_APP_ABI_VERSION);
    t5_app_frame_t frame={0};
    return host && host->struct_size>=offsetof(t5_app_api_v1,touch_contact)+sizeof(host->touch_contact) &&
        host->copy_ui_frame && host->touch_contact && host->copy_ui_frame(NULL,0,&frame)
        ? T5_HARDWARE_TAKEOVER_DISPLAY|T5_HARDWARE_TAKEOVER_UI_VIDEO : 0;
}

__attribute__((visibility("default"))) void app_main(void) {
    api = t5_app_get_api(T5_APP_ABI_VERSION);
    storage = t5_storage_get_api(T5_STORAGE_API_VERSION);
    if (!has_required_app_api()) return;
    api->installed_apps_refresh();
    count = api->installed_apps_count();
    if (count > MAX_HOME_APPS) count = MAX_HOME_APPS;
    selected = 0;
    edit_mode = false;
    selection_visible = false;
    load_home_pins();
    layout();
    sv_fatal=false;sv_video=NULL;
    (void)sv_open();
    if(sv_fatal) goto cleanup;
    draw(0);

    t5_app_input_t input;
    uint32_t previous_buttons = 0;
    uint32_t confirm_started = 0;
    bool confirm_hold_handled = false;

    while (api->poll(&input, sv_enabled ? 5 : 20)) {
        if (input.exit_requested || sv_fatal) break;
        t5_app_swipe_t swipe={0};
        const size_t swipe_api_size=offsetof(t5_app_api_v1,take_touch_swipe)+sizeof(api->take_touch_swipe);
        const bool swiped=api->struct_size>=swipe_api_size && api->take_touch_swipe && api->take_touch_swipe(&swipe);
        const bool gesture_consumed=sv_input(&input,swiped,&swipe);
        sv_present();
        if(gesture_consumed) {previous_buttons=input.buttons;continue;}
        const uint32_t pressed = input.buttons & ~previous_buttons;
        const uint32_t released = previous_buttons & ~input.buttons;
        uint32_t old = selected;

        if (count) {
            const uint32_t before_navigation = selected;
            if (pressed & T5_APP_BUTTON_LEFT) selected = selected ? selected - 1 : count - 1;
            if (pressed & T5_APP_BUTTON_RIGHT) selected = (selected + 1) % count;
            if (pressed & T5_APP_BUTTON_UP) selected = selected >= (uint32_t)columns ? selected - columns : 0;
            if (pressed & T5_APP_BUTTON_DOWN) selected = selected + columns < count ? selected + columns : count - 1;
            if (selected != before_navigation) selection_visible = true;

            if (pressed & T5_APP_BUTTON_CONFIRM) {
                confirm_started = api->millis();
                confirm_hold_handled = false;
            }
            if ((input.buttons & T5_APP_BUTTON_CONFIRM) && !confirm_hold_handled &&
                api->millis() - confirm_started >= CONFIRM_HOLD_MS) {
                toggle_edit_mode();
                confirm_hold_handled = true;
            }
            if ((released & T5_APP_BUTTON_CONFIRM) && !confirm_hold_handled) {
                if (edit_mode) {
                    toggle_home();
                } else if (launch()) {
                    goto cleanup;
                }
            }
        }

        // Swipes and taps are separate completed gestures. Handle a swipe first
        // so dragging across an icon cannot launch it or toggle a Home pin.
        if (swiped && !sv_enabled) {
            const int dx = (int)swipe.end_x - swipe.start_x;
            const int dy = (int)swipe.end_y - swipe.start_y;
            // Ignore vertical/ambiguous diagonal gestures and short drags.
            if (abs(dx) >= 50 && abs(dx) > 2 * abs(dy) && page_count() > 1u) {
                change_page(dx < 0);
                draw(0);
                old = selected;
            }
        }

        if (input.tapped && !swiped) {
            const int x = input.touch_x, y = input.touch_y;
            if (edit_button_hit(x, y)) {
                selection_visible = false;
                toggle_edit_mode();
                old = selected;
            } else if (page_dots_hit(x, y)) {
                change_page(true);
                draw(0);
                old = selected;
            } else if (count && x >= 16 && x < 16 + columns * cell_w &&
                       y >= 80 && y < 80 + rows * cell_h) {
                const uint32_t tapped = current_page() * (uint32_t)page_size +
                    (uint32_t)((y - 80) / cell_h) * (uint32_t)columns +
                    (uint32_t)((x - 16) / cell_w);
                if (tapped < count) {
                    selected = tapped;
                    selection_visible = false;
                    if (edit_mode) {
                        toggle_home();
                        old = selected;
                    } else {
                        if (launch()) goto cleanup;
                        old = selected;
                    }
                }
            }
        }

        previous_buttons = input.buttons;
        if (selected != old) draw(0);
    }
cleanup:
    if(sv_fatal && api->log_message) api->log_message("Springboard video unavailable/stalled; returning to Home");
    sv_close();
}
