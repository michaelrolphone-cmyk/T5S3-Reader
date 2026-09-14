#include "T5AppApi.h"

#include <stdio.h>
#include <string.h>

#define HEADER_Y 28
#define FIRST_ROW_Y 82
#define ROW_HEIGHT 28
#define FOOTER_HEIGHT 82
#define LINE_MAX 96

static bool has_catalog_api(const t5_app_api_v1 *api) {
    const size_t required = offsetof(t5_app_api_v1, app_catalog_download) + sizeof(api->app_catalog_download);
    return api && api->struct_size >= required && api->app_catalog_refresh && api->app_catalog_count &&
           api->app_catalog_get && api->app_catalog_download;
}

static int32_t visible_rows(const t5_app_api_v1 *api) {
    int32_t rows = (api->screen_height() - FIRST_ROW_Y - FOOTER_HEIGHT) / ROW_HEIGHT;
    if (rows < 1) rows = 1;
    if (rows > 18) rows = 18;
    return rows;
}

static void draw_status(const t5_app_api_v1 *api, const char *title, const char *line1, const char *line2) {
    api->clear();
    api->draw_text(24, HEADER_Y, title);
    if (line1) api->draw_text(24, HEADER_Y + 52, line1);
    if (line2) api->draw_text(24, HEADER_Y + 84, line2);
    api->present(true);
}

static void format_size(char *out, size_t capacity, uint64_t bytes) {
    if (bytes >= 1024u * 1024u) {
        const unsigned long whole = (unsigned long)(bytes / (1024u * 1024u));
        const unsigned long tenth = (unsigned long)((bytes % (1024u * 1024u)) * 10u / (1024u * 1024u));
        snprintf(out, capacity, "%lu.%lu MB", whole, tenth);
    } else if (bytes >= 1024u) {
        snprintf(out, capacity, "%lu KB", (unsigned long)(bytes / 1024u));
    } else {
        snprintf(out, capacity, "%lu B", (unsigned long)bytes);
    }
}

static void draw_catalog(const t5_app_api_v1 *api, uint32_t selected) {
    const uint32_t count = api->app_catalog_count();
    const int32_t height = api->screen_height();
    const int32_t rows = visible_rows(api);

    api->clear();
    api->draw_text(24, HEADER_Y, "Native App Store");

    if (count == 0) {
        api->draw_text(24, FIRST_ROW_Y, "No .elf assets in latest release.");
        api->draw_text(24, height - 50, "Right: refresh   Back: exit");
        api->present(true);
        return;
    }

    const uint32_t page_start = (selected / (uint32_t)rows) * (uint32_t)rows;
    for (int32_t row = 0; row < rows; ++row) {
        const uint32_t index = page_start + (uint32_t)row;
        if (index >= count) break;

        t5_app_release_asset_t asset;
        if (!api->app_catalog_get(index, &asset)) continue;

        char size_text[24];
        char line[LINE_MAX];
        format_size(size_text, sizeof(size_text), asset.size);
        snprintf(line, sizeof(line), "%c %s  %s", index == selected ? '>' : ' ', asset.name, size_text);
        api->draw_text(24, FIRST_ROW_Y + row * ROW_HEIGHT, line);
    }

    char page[48];
    snprintf(page, sizeof(page), "%lu app%s", (unsigned long)count, count == 1 ? "" : "s");
    api->draw_text(24, height - 74, page);
    api->draw_text(24, height - 48, "Up/Down: select  Confirm/tap: install  Right: refresh");
    api->present(true);
}

static bool refresh_catalog(const t5_app_api_v1 *api) {
    draw_status(api, "Native App Store", "Connecting with saved Wi-Fi...", "Fetching latest release...");
    if (!api->app_catalog_refresh()) {
        draw_status(api, "Native App Store", "Unable to load latest release.", "Check saved Wi-Fi and try Right.");
        return false;
    }
    return true;
}

static bool select_tapped_row(const t5_app_api_v1 *api, const t5_app_input_t *input, uint32_t count,
                              uint32_t *selected) {
    if (!input->tapped || !selected || count == 0) return false;

    const int32_t rows = visible_rows(api);
    const int32_t list_bottom = FIRST_ROW_Y + rows * ROW_HEIGHT;
    if (input->touch_y < FIRST_ROW_Y || input->touch_y >= list_bottom) return false;

    const uint32_t page_start = (*selected / (uint32_t)rows) * (uint32_t)rows;
    const uint32_t row = (uint32_t)((input->touch_y - FIRST_ROW_Y) / ROW_HEIGHT);
    const uint32_t tapped = page_start + row;
    if (tapped >= count) return false;

    *selected = tapped;
    return true;
}

__attribute__((visibility("default"))) void app_main(void) {
    const t5_app_api_v1 *api = t5_app_get_api(T5_APP_ABI_VERSION);
    if (!has_catalog_api(api)) return;

    uint32_t selected = 0;
    bool loaded = refresh_catalog(api);
    if (loaded) draw_catalog(api, selected);

    bool armed = false;
    t5_app_input_t input;
    while (api->poll(&input, 20)) {
        if (input.exit_requested) break;

        const uint32_t buttons = input.buttons;
        if (buttons == 0 && !input.tapped) {
            armed = true;
            continue;
        }
        if (!armed) continue;
        armed = false;

        if (buttons & T5_APP_BUTTON_RIGHT) {
            loaded = refresh_catalog(api);
            selected = 0;
            if (loaded) draw_catalog(api, selected);
            continue;
        }

        if (!loaded) continue;

        const uint32_t count = api->app_catalog_count();
        if (count == 0) continue;

        if (buttons & T5_APP_BUTTON_UP) {
            selected = selected == 0 ? count - 1 : selected - 1;
            draw_catalog(api, selected);
            continue;
        }
        if (buttons & T5_APP_BUTTON_DOWN) {
            selected = (selected + 1) % count;
            draw_catalog(api, selected);
            continue;
        }

        const bool tapped_row = select_tapped_row(api, &input, count, &selected);
        if ((buttons & T5_APP_BUTTON_CONFIRM) || tapped_row) {
            t5_app_release_asset_t asset;
            if (!api->app_catalog_get(selected, &asset)) continue;

            char installing[LINE_MAX];
            snprintf(installing, sizeof(installing), "Installing %s", asset.name);
            draw_status(api, "Native App Store", installing, "Saving to /sd/Apps ...");

            if (api->app_catalog_download(selected)) {
                char installed[LINE_MAX];
                snprintf(installed, sizeof(installed), "Installed: %s", asset.name);
                draw_status(api, "Native App Store", installed, "Confirm/Down: back to list");
            } else {
                draw_status(api, "Native App Store", "Download failed.", "Confirm/Down: back to list");
            }

            bool wait_release = true;
            t5_app_input_t acknowledge;
            while (api->poll(&acknowledge, 20)) {
                if (acknowledge.exit_requested) return;
                if (acknowledge.buttons == 0 && !acknowledge.tapped) {
                    wait_release = false;
                    continue;
                }
                if (!wait_release && ((acknowledge.buttons & (T5_APP_BUTTON_CONFIRM | T5_APP_BUTTON_DOWN)) ||
                                     acknowledge.tapped)) {
                    break;
                }
            }
            draw_catalog(api, selected);
        }
    }
}
