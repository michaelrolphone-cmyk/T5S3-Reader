#include "T5AppApi.h"
#include "T5FileBrowserApi.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_CATALOG_ITEMS 64u
#define UI_LABEL_MAX 192u
#define STATUS_MAX 192u
#define APP_STORE_PATH "/App Store"

typedef enum {
    APP_ACTION_INSTALL = 0,
    APP_ACTION_UPDATE = 1,
    APP_ACTION_CURRENT = 2,
} app_action_t;

static char ui_labels[MAX_CATALOG_ITEMS][UI_LABEL_MAX];
static t5_file_browser_entry_t ui_entries[MAX_CATALOG_ITEMS];
static uint32_t ui_count;

static bool has_catalog_api(const t5_app_api_v1 *api) {
    const size_t required = offsetof(t5_app_api_v1, app_catalog_download) + sizeof(api->app_catalog_download);
    return api && api->struct_size >= required && api->app_catalog_refresh && api->app_catalog_count &&
           api->app_catalog_get && api->app_catalog_download;
}

static bool has_catalog_manifest_api(const t5_app_api_v1 *api) {
    const size_t required =
        offsetof(t5_app_api_v1, app_catalog_manifest_get) + sizeof(api->app_catalog_manifest_get);
    return api && api->struct_size >= required && api->app_catalog_manifest_get;
}

static bool has_version_api(const t5_app_api_v1 *api) {
    const size_t required =
        offsetof(t5_app_api_v1, app_catalog_version_get) + sizeof(api->app_catalog_version_get);
    return api && api->struct_size >= required && api->installed_app_version_get && api->app_catalog_version_get;
}

static bool has_browser_api(const t5_file_browser_api_v1 *browser) {
    const size_t required = offsetof(t5_file_browser_api_v1, page_items) + sizeof(browser->page_items);
    return browser && browser->struct_size >= required && browser->render && browser->poll_event && browser->page_items;
}

static bool catalog_manifest(const t5_app_api_v1 *api, uint32_t index, t5_app_manifest_t *manifest) {
    if (!manifest || !has_catalog_manifest_api(api)) return false;
    *manifest = (t5_app_manifest_t){0};
    return api->app_catalog_manifest_get(index, manifest);
}

static const char *catalog_display_name(const t5_app_api_v1 *api, uint32_t index,
                                        t5_app_release_asset_t *asset, t5_app_manifest_t *manifest,
                                        bool *has_manifest) {
    if (has_manifest) *has_manifest = false;
    if (!api->app_catalog_get(index, asset)) return NULL;
    if (catalog_manifest(api, index, manifest)) {
        if (has_manifest) *has_manifest = true;
        return manifest->display_name;
    }
    return asset->name;
}

static app_action_t catalog_action(const t5_app_api_v1 *api, uint32_t index,
                                   const t5_app_manifest_t *manifest,
                                   char *available_version, size_t available_capacity,
                                   char *installed_version, size_t installed_capacity) {
    if (available_version && available_capacity) available_version[0] = '\0';
    if (installed_version && installed_capacity) installed_version[0] = '\0';
    if (!manifest || !has_version_api(api)) return APP_ACTION_INSTALL;
    if (!api->app_catalog_version_get(index, available_version, available_capacity)) return APP_ACTION_INSTALL;
    if (!api->installed_app_version_get(manifest->file_name, installed_version, installed_capacity)) {
        return APP_ACTION_INSTALL;
    }
    if (!strcmp(available_version, installed_version)) return APP_ACTION_CURRENT;
    return APP_ACTION_UPDATE;
}

static int32_t next_index(int32_t current, uint32_t count) {
    return count ? (current + 1) % (int32_t)count : 0;
}

static int32_t previous_index(int32_t current, uint32_t count) {
    return count ? (current + (int32_t)count - 1) % (int32_t)count : 0;
}

static int32_t next_page(int32_t current, uint32_t count, uint32_t page_items) {
    if (!count || !page_items) return 0;
    if (count <= page_items) return next_index(current, count);
    const int32_t last_page = ((int32_t)count - 1) / (int32_t)page_items;
    const int32_t page = current / (int32_t)page_items;
    return page < last_page ? (page + 1) * (int32_t)page_items : 0;
}

static int32_t previous_page(int32_t current, uint32_t count, uint32_t page_items) {
    if (!count || !page_items) return 0;
    if (count <= page_items) return previous_index(current, count);
    const int32_t last_page = ((int32_t)count - 1) / (int32_t)page_items;
    const int32_t page = current / (int32_t)page_items;
    return page > 0 ? (page - 1) * (int32_t)page_items : last_page * (int32_t)page_items;
}

static void build_ui_entries(const t5_app_api_v1 *api) {
    uint32_t count = api->app_catalog_count();
    if (count > MAX_CATALOG_ITEMS) count = MAX_CATALOG_ITEMS;
    ui_count = 0;

    for (uint32_t i = 0; i < count; ++i) {
        t5_app_release_asset_t asset = {0};
        t5_app_manifest_t manifest = {0};
        bool has_manifest = false;
        const char *name = catalog_display_name(api, i, &asset, &manifest, &has_manifest);
        if (!name) continue;

        char available[T5_APP_VERSION_MAX] = {0};
        char installed[T5_APP_VERSION_MAX] = {0};
        const app_action_t action = has_manifest
            ? catalog_action(api, i, &manifest, available, sizeof(available), installed, sizeof(installed))
            : APP_ACTION_INSTALL;

        const char *state = "Install";
        if (has_manifest && !manifest.compatible) state = "Firmware update required";
        else if (action == APP_ACTION_UPDATE) state = "Update";
        else if (action == APP_ACTION_CURRENT) state = "Installed";

        snprintf(ui_labels[ui_count], sizeof(ui_labels[ui_count]), "%s [%s].elf", name, state);
        ui_entries[ui_count].name = ui_labels[ui_count];
        ui_entries[ui_count].is_directory = false;
        ++ui_count;
    }
}

static void selected_status(const t5_app_api_v1 *api, int32_t selected, char *status, size_t capacity) {
    if (!status || capacity == 0) return;
    status[0] = '\0';
    if (selected < 0 || selected >= (int32_t)ui_count) return;

    t5_app_release_asset_t asset = {0};
    t5_app_manifest_t manifest = {0};
    bool has_manifest = false;
    const char *name = catalog_display_name(api, (uint32_t)selected, &asset, &manifest, &has_manifest);
    if (!name) return;

    char available[T5_APP_VERSION_MAX] = {0};
    char installed[T5_APP_VERSION_MAX] = {0};
    const app_action_t action = has_manifest
        ? catalog_action(api, (uint32_t)selected, &manifest, available, sizeof(available), installed, sizeof(installed))
        : APP_ACTION_INSTALL;

    if (has_manifest && !manifest.compatible) {
        snprintf(status, capacity, "%s: requires newer firmware", name);
    } else if (action == APP_ACTION_CURRENT) {
        snprintf(status, capacity, "%s: installed%s%s", name, installed[0] ? " " : "", installed);
    } else if (action == APP_ACTION_UPDATE) {
        if (installed[0] && available[0])
            snprintf(status, capacity, "%s: update %s -> %s", name, installed, available);
        else
            snprintf(status, capacity, "%s: update available", name);
    } else if (available[0]) {
        snprintf(status, capacity, "%s: install %s", name, available);
    } else {
        snprintf(status, capacity, "%s: install", name);
    }
}

static void render_catalog(const t5_app_api_v1 *api, const t5_file_browser_api_v1 *browser,
                           int32_t selected, const char *override_status) {
    char status[STATUS_MAX];
    if (override_status && override_status[0]) {
        snprintf(status, sizeof(status), "%s", override_status);
    } else {
        selected_status(api, selected, status, sizeof(status));
    }
    browser->render(APP_STORE_PATH, status, ui_entries, ui_count, selected);
}

static void render_loading(const t5_file_browser_api_v1 *browser, const char *status) {
    static const t5_file_browser_entry_t loading_entry = {"Loading release catalog.elf", false};
    browser->render(APP_STORE_PATH, status ? status : "Loading...", &loading_entry, 1, 0);
}

static bool refresh_catalog(const t5_app_api_v1 *api, const t5_file_browser_api_v1 *browser) {
    render_loading(browser, "Connecting with saved Wi-Fi...");
    if (!api->app_catalog_refresh()) {
        static const t5_file_browser_entry_t retry_entry = {"Retry catalog refresh.elf", false};
        browser->render(APP_STORE_PATH, "Unable to load latest release - Confirm to retry", &retry_entry, 1, 0);
        return false;
    }
    build_ui_entries(api);
    return true;
}

static bool wait_for_retry_or_exit(const t5_file_browser_api_v1 *browser) {
    for (;;) {
        t5_file_browser_event_t event = {0};
        if (!browser->poll_event(&event, 20, true, false)) return false;
        switch (event.type) {
            case T5_FILE_BROWSER_EVENT_OPEN:
            case T5_FILE_BROWSER_EVENT_ROW:
                return true;
            case T5_FILE_BROWSER_EVENT_BACK:
            case T5_FILE_BROWSER_EVENT_ROOT:
            case T5_FILE_BROWSER_EVENT_EXIT:
                return false;
            default:
                break;
        }
    }
}

static void activate_selected(const t5_app_api_v1 *api, const t5_file_browser_api_v1 *browser,
                              int32_t selected) {
    if (selected < 0 || selected >= (int32_t)ui_count) return;

    t5_app_release_asset_t asset = {0};
    t5_app_manifest_t manifest = {0};
    bool has_manifest = false;
    const char *name_ptr = catalog_display_name(api, (uint32_t)selected, &asset, &manifest, &has_manifest);
    if (!name_ptr) return;

    char name[96];
    snprintf(name, sizeof(name), "%s", name_ptr);
    char available[T5_APP_VERSION_MAX] = {0};
    char installed[T5_APP_VERSION_MAX] = {0};
    const app_action_t action = has_manifest
        ? catalog_action(api, (uint32_t)selected, &manifest, available, sizeof(available), installed, sizeof(installed))
        : APP_ACTION_INSTALL;

    char status[STATUS_MAX];
    if (has_manifest && !manifest.compatible) {
        snprintf(status, sizeof(status), "%s requires newer firmware", name);
        render_catalog(api, browser, selected, status);
        return;
    }
    if (action == APP_ACTION_CURRENT) {
        snprintf(status, sizeof(status), "%s is already current", name);
        render_catalog(api, browser, selected, status);
        return;
    }

    snprintf(status, sizeof(status), "%s %s...", action == APP_ACTION_UPDATE ? "Updating" : "Installing", name);
    render_catalog(api, browser, selected, status);

    const bool ok = api->app_catalog_download((uint32_t)selected);
    build_ui_entries(api);
    if (ui_count == 0) selected = 0;
    else if (selected >= (int32_t)ui_count) selected = (int32_t)ui_count - 1;

    if (ok)
        snprintf(status, sizeof(status), "%s: %s", name, action == APP_ACTION_UPDATE ? "updated" : "installed");
    else
        snprintf(status, sizeof(status), "%s: %s failed", name, action == APP_ACTION_UPDATE ? "update" : "install");
    render_catalog(api, browser, selected, status);
}

__attribute__((visibility("default"))) void app_main(void) {
    const t5_app_api_v1 *api = t5_app_get_api(T5_APP_ABI_VERSION);
    const t5_file_browser_api_v1 *browser = t5_file_browser_get_api(T5_FILE_BROWSER_API_VERSION);
    if (!has_catalog_api(api) || !has_browser_api(browser) || !api->set_back_exits_app) return;

    api->set_back_exits_app(false);

    bool loaded = refresh_catalog(api, browser);
    while (!loaded) {
        if (!wait_for_retry_or_exit(browser)) {
            api->set_back_exits_app(true);
            return;
        }
        loaded = refresh_catalog(api, browser);
    }

    int32_t selected = 0;
    render_catalog(api, browser, selected, ui_count ? NULL : "No release apps found");

    for (;;) {
        t5_file_browser_event_t event = {0};
        if (!browser->poll_event(&event, 20, true, false)) break;
        bool redraw = false;

        switch (event.type) {
            case T5_FILE_BROWSER_EVENT_PREVIOUS:
                selected = previous_index(selected, ui_count);
                redraw = true;
                break;
            case T5_FILE_BROWSER_EVENT_NEXT:
                selected = next_index(selected, ui_count);
                redraw = true;
                break;
            case T5_FILE_BROWSER_EVENT_PAGE_PREVIOUS:
                selected = previous_page(selected, ui_count, browser->page_items());
                redraw = true;
                break;
            case T5_FILE_BROWSER_EVENT_PAGE_NEXT:
                selected = next_page(selected, ui_count, browser->page_items());
                redraw = true;
                break;
            case T5_FILE_BROWSER_EVENT_ROW:
                if (event.row_index >= 0 && event.row_index < (int32_t)ui_count) {
                    selected = event.row_index;
                    activate_selected(api, browser, selected);
                }
                break;
            case T5_FILE_BROWSER_EVENT_OPEN:
                activate_selected(api, browser, selected);
                break;
            case T5_FILE_BROWSER_EVENT_BACK:
            case T5_FILE_BROWSER_EVENT_ROOT:
            case T5_FILE_BROWSER_EVENT_EXIT:
                api->set_back_exits_app(true);
                return;
            default:
                break;
        }

        if (redraw) render_catalog(api, browser, selected, NULL);
    }

    api->set_back_exits_app(true);
}
