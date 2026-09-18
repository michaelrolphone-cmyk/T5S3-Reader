#include "T5AppApi.h"
#include "T5PackageManagerApi.h"
#include "T5UiApi.h"
#include <assert.h>
#include <string.h>

void app_main(void);
static int downloads, saw_alpha, saw_beta, saw_installed, saw_update;
static int back_disabled, back_restored, event_index;
static uint32_t downloaded_index;
static int preview_calls, install_calls;
static int32_t width(void) { return 540; }
static int32_t height(void) { return 960; }
static void clear(void) {}
static void text(int32_t x, int32_t y, const char *s) { (void)x; (void)y; (void)s; }
static void rect(int32_t x, int32_t y, int32_t w, int32_t h, bool black) {
    (void)black;
    assert(x >= 0 && y >= 0 && w >= 0 && h >= 0);
    assert(x + w <= 540 && y + h <= 960);
}
static void present(bool full) { (void)full; }
static bool refresh(void) { return true; }
static uint32_t count(void) { return 2; }
static bool asset_get(uint32_t index, t5_app_release_asset_t *asset) {
    assert(index < count());
    memset(asset, 0, sizeof(*asset));
    strcpy(asset->name, index == 0 ? "alpha.elf" : "beta.elf");
    asset->size = 1234 + index;
    return true;
}
static bool manifest_get(uint32_t index, t5_app_manifest_t *manifest) {
    assert(index < count());
    memset(manifest, 0, sizeof(*manifest));
    strcpy(manifest->display_name, index == 0 ? "Alpha" : "Beta");
    strcpy(manifest->file_name, index == 0 ? "alpha.elf" : "beta.elf");
    strcpy(manifest->min_firmware_version, "1.1.5");
    strcpy(manifest->icon, index == 0 ? "solid:f013" : "regular:f007");
    manifest->compatible = true;
    return true;
}
static bool installed_version_get(const char *file_name, char *version, size_t capacity) {
    assert(file_name && version && capacity >= T5_APP_VERSION_MAX);
    if (!strcmp(file_name, "alpha.elf")) {
        strcpy(version, "1.0.0");
        return true;
    }
    if (!strcmp(file_name, "beta.elf")) {
        strcpy(version, "0.9.0");
        return true;
    }
    return false;
}
static bool catalog_version_get(uint32_t index, char *version, size_t capacity) {
    assert(index < count() && version && capacity >= T5_APP_VERSION_MAX);
    strcpy(version, "1.0.0");
    return true;
}
static bool download(uint32_t index) {
    assert(index < count());
    ++downloads;
    downloaded_index = index;
    return true;
}
static bool poll(t5_app_input_t *input, uint32_t wait_ms) {
    (void)wait_ms;
    memset(input, 0, sizeof(*input));
    return true;
}
static void set_back_exits_app(bool enabled) {
    if (enabled) back_restored = 1;
    else back_disabled = 1;
}
static bool directory_open(const char *path) {
    assert(path && !strcmp(path, "/sd/Packages/Inbox"));
    return true;
}
static bool directory_next(t5_app_dirent_t *out) {
    assert(out);
    return false; // Empty offline inventory in release-view regression.
}
static void directory_close(void) {}
static const t5_app_api_v1 api = {
    .abi_version = T5_APP_ABI_VERSION,
    .struct_size = sizeof(t5_app_api_v1),
    .screen_width = width,
    .screen_height = height,
    .clear = clear,
    .draw_text = text,
    .fill_rect = rect,
    .present = present,
    .poll = poll,
    .dir_open = directory_open,
    .dir_next = directory_next,
    .dir_close = directory_close,
    .app_catalog_refresh = refresh,
    .app_catalog_count = count,
    .app_catalog_get = asset_get,
    .app_catalog_download = download,
    .set_back_exits_app = set_back_exits_app,
    .app_catalog_manifest_get = manifest_get,
    .installed_app_version_get = installed_version_get,
    .app_catalog_version_get = catalog_version_get,
};
const t5_app_api_v1 *t5_app_get_api(uint32_t version) {
    assert(version == T5_APP_ABI_VERSION);
    return &api;
}
static bool preview(const char *folder, t5_package_preview_t *out) {
    assert(folder && out);
    ++preview_calls;
    memset(out, 0, sizeof(*out));
    return false;
}
static bool install(const char *folder) { assert(folder); ++install_calls; return true; }
static bool uninstall(uint8_t kind, const char *id) {
    (void)kind; (void)id;
    return true;
}
static const t5_package_manager_api_v1 package_api = {
    T5_PACKAGE_MANAGER_API_VERSION, sizeof(t5_package_manager_api_v1),
    preview, install, uninstall,
};
const t5_package_manager_api_v1 *t5_package_manager_get_api(uint32_t version) {
    assert(version == T5_PACKAGE_MANAGER_API_VERSION);
    return &package_api;
}
static void render_list(const t5_ui_chrome_t *chrome,
                        const t5_ui_list_row_t *rows,
                        uint32_t row_count,
                        int32_t selected_index) {
    assert(chrome && selected_index >= 0);
    if (row_count == 2) {
        assert(rows);
        for (uint32_t i = 0; i < row_count; ++i) {
            if (rows[i].title && !strcmp(rows[i].title, "Alpha")) saw_alpha = 1;
            if (rows[i].title && !strcmp(rows[i].title, "Beta")) saw_beta = 1;
            if (rows[i].subtitle && !strcmp(rows[i].subtitle, "Installed")) saw_installed = 1;
            if ((rows[i].flags & T5_UI_LIST_HIGHLIGHT_VALUE) != 0) saw_update = 1;
        }
        if (chrome->confirm_label && !strcmp(chrome->confirm_label, "Update")) saw_update = 1;
    }
}
static int32_t hit_test(int16_t x, int16_t y) {
    (void)x; (void)y; return T5_UI_HIT_NONE;
}
static bool poll_event(t5_ui_event_t *event, uint32_t wait_ms) {
    (void)wait_ms;
    assert(event);
    memset(event, 0, sizeof(*event));
    switch (event_index++) {
        case 0: event->type = T5_UI_EVENT_NEXT; break;
        case 1: event->type = T5_UI_EVENT_CONFIRM; break;
        default: event->type = T5_UI_EVENT_EXIT; break;
    }
    return true;
}
static int32_t next_index(int32_t current_index, uint32_t item_count) {
    return item_count ? (current_index + 1) % (int32_t)item_count : 0;
}
static int32_t previous_index(int32_t current_index, uint32_t item_count) {
    return !item_count ? 0 : current_index <= 0 ?
        (int32_t)item_count - 1 : current_index - 1;
}
static const t5_ui_api_v1 ui_api = {
    .api_version = T5_UI_API_VERSION,
    .struct_size = sizeof(t5_ui_api_v1),
    .render_list = render_list,
    .hit_test = hit_test,
    .poll_event = poll_event,
    .next_index = next_index,
    .previous_index = previous_index,
};
const t5_ui_api_v1 *t5_ui_get_api(uint32_t version) {
    assert(version == T5_UI_API_VERSION);
    return &ui_api;
}
int main(void) {
    app_main();
    assert(saw_alpha && saw_beta && saw_installed && saw_update);
    assert(downloads == 1 && downloaded_index == 1);
    assert(install_calls == 0 && preview_calls == 0);
    assert(back_disabled && back_restored);
    return 0;
}
