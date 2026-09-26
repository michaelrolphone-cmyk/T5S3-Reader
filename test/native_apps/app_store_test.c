#include "T5AppApi.h"
#include "T5PackageManagerApi.h"
#include "T5UiApi.h"
#include <assert.h>
#include <string.h>

void app_main(void);
static int online_refreshes, online_installs, saw_alpha, saw_beta, saw_installed, saw_update;
static int back_disabled, back_restored, event_index;
static uint32_t installed_index;
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
    return false;
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
    .set_back_exits_app = set_back_exits_app,
    .app_catalog_download_last_error = download_last_error,
    .app_catalog_download_with_progress = download_with_progress,
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
static bool preview_archive(const char *archive, t5_package_preview_t *out) {
    (void)archive; (void)out;
    return false;
}
static bool install_archive(const char *archive) { (void)archive; return false; }
static bool online_refresh(void) { ++online_refreshes; return true; }
static uint32_t online_count(void) { return 2; }
static bool online_get(uint32_t index, t5_package_catalog_row_t *out) {
    assert(index < online_count() && out);
    memset(out, 0, sizeof(*out));
    out->package.kind = T5_PACKAGE_APPLICATION;
    out->package.valid_installation = 1;
    strcpy(out->package.id, index == 0 ? "alpha" : "beta");
    strcpy(out->package.version, "1.0.0");
    strcpy(out->package.artifact, "app.elf");
    strcpy(out->archive, index == 0 ? "application-alpha-1.0.0-xtensa-esp32s3.rte.zip" :
                                     "application-beta-1.0.0-xtensa-esp32s3.rte.zip");
    if (index == 0) {
        strcpy(out->package.installed_version, "1.0.0");
        out->package.install_allowed = 0;
    } else {
        strcpy(out->package.installed_version, "0.9.0");
        out->package.install_allowed = 1;
    }
    return true;
}
static bool online_install(uint32_t index) {
    assert(index < online_count());
    ++online_installs;
    installed_index = index;
    return true;
}
static const t5_package_manager_api_v1 package_api = {
    .api_version = T5_PACKAGE_MANAGER_API_VERSION,
    .struct_size = sizeof(t5_package_manager_api_v1),
    .preview = preview,
    .install = install,
    .uninstall = uninstall,
    .preview_archive = preview_archive,
    .install_archive = install_archive,
    .online_refresh = online_refresh,
    .online_count = online_count,
    .online_get = online_get,
    .online_install = online_install,
};
const t5_package_manager_api_v1 *t5_package_manager_get_api(uint32_t version) {
    assert(version == T5_PACKAGE_MANAGER_API_VERSION);
    return &package_api;
}

static void render_list(const t5_ui_chrome_t *chrome,
                        const t5_ui_list_row_t *list,
                        uint32_t count,
                        int32_t selected_index) {
    assert(chrome && selected_index >= 0);
    if (chrome->status && strstr(chrome->status, "Beta: HTTP download failed"))
        saw_failure_detail = 1;
    if (chrome->status && strstr(chrome->status, "Downloading release |")) {
        assert(row_count == 1 && rows && rows[0].title &&
               !strcmp(rows[0].title, "Download progress"));
        saw_download_progress = 1;
        ++progress_callbacks;
    }
    if (count == 2) {
        assert(list);
        for (uint32_t i = 0; i < count; ++i) {
            if (list[i].title && !strcmp(list[i].title, "alpha")) saw_alpha = 1;
            if (list[i].title && !strcmp(list[i].title, "beta")) saw_beta = 1;
            if (list[i].subtitle && strstr(list[i].subtitle, "Installed 1.0.0")) saw_installed = 1;
            if ((list[i].flags & T5_UI_LIST_HIGHLIGHT_VALUE) != 0) saw_update = 1;
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
    assert(online_refreshes == 1);
    assert(online_installs == 1 && installed_index == 1);
    assert(saw_failure_detail);
    assert(saw_download_progress && progress_callbacks == 3);
    assert(install_calls == 0 && preview_calls == 0);
    assert(back_disabled && back_restored);
    return 0;
}
