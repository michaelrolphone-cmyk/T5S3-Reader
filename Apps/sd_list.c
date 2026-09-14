#include "T5AppApi.h"

#define HEADER_Y 30
#define FIRST_ROW_Y 90
#define ROW_HEIGHT 26
#define FOOTER_HEIGHT 70
#define LINE_MAX 64

static bool has_directory_api(const t5_app_api_v1 *api) {
    const size_t required = offsetof(t5_app_api_v1, dir_close) + sizeof(api->dir_close);
    return api && api->struct_size >= required && api->dir_open && api->dir_next && api->dir_close;
}

static void append_text(char *dst, size_t capacity, const char *src) {
    size_t d = 0;
    size_t s = 0;
    if (!dst || !src || capacity == 0) return;
    while (d + 1 < capacity && dst[d]) ++d;
    while (d + 1 < capacity && src[s]) dst[d++] = src[s++];
    dst[d] = '\0';
}

static void format_entry(char *line, size_t capacity, const t5_app_dirent_t *entry) {
    line[0] = '\0';
    append_text(line, capacity, entry->is_directory ? "[DIR] " : "      ");
    append_text(line, capacity, entry->name);
}

static bool draw_next_page(const t5_app_api_v1 *api) {
    const int32_t height = api->screen_height();
    int32_t rows = (height - FIRST_ROW_Y - FOOTER_HEIGHT) / ROW_HEIGHT;
    if (rows < 1) rows = 1;
    if (rows > 30) rows = 30;

    api->clear();
    api->draw_text(24, HEADER_Y, "SD card: /sd");

    bool end = false;
    int32_t row = 0;
    for (; row < rows; ++row) {
        t5_app_dirent_t entry;
        if (!api->dir_next(&entry)) {
            end = true;
            break;
        }
        char line[LINE_MAX];
        format_entry(line, sizeof(line), &entry);
        api->draw_text(24, FIRST_ROW_Y + row * ROW_HEIGHT, line);
    }

    if (row == 0 && end) {
        api->draw_text(24, FIRST_ROW_Y, "(empty or end of directory)");
    }

    if (end) {
        api->draw_text(24, height - 48, "Confirm / tap: restart   Back: exit");
    } else {
        api->draw_text(24, height - 48, "Confirm / tap / Down: next   Back: exit");
    }
    api->present(true);
    return end;
}

__attribute__((visibility("default"))) void app_main(void) {
    const t5_app_api_v1 *api = t5_app_get_api(T5_APP_ABI_VERSION);
    if (!has_directory_api(api)) return;

    api->clear();
    if (!api->dir_open("/sd")) {
        api->draw_text(24, HEADER_Y, "Unable to open /sd");
        api->draw_text(24, HEADER_Y + 40, "Back / PWR / Home exits.");
        api->present(true);
        t5_app_input_t input;
        while (api->poll(&input, 20) && !input.exit_requested) {}
        return;
    }

    bool end = draw_next_page(api);
    bool armed = false;
    t5_app_input_t input;
    while (api->poll(&input, 20)) {
        if (input.exit_requested) break;

        const bool next = input.tapped ||
            (input.buttons & (T5_APP_BUTTON_CONFIRM | T5_APP_BUTTON_DOWN | T5_APP_BUTTON_RIGHT));
        if (!next) {
            armed = true;
            continue;
        }
        if (!armed) continue;
        armed = false;

        if (end) {
            api->dir_close();
            if (!api->dir_open("/sd")) break;
        }
        end = draw_next_page(api);
    }

    api->dir_close();
}
