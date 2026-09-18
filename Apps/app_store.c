#include "T5AppApi.h"
#include "T5PackageManagerApi.h"
#include "T5UiApi.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_CATALOG_ITEMS 64u
#define TITLE_MAX 96u
#define SUBTITLE_MAX 96u
#define VALUE_MAX 40u
#define STATUS_MAX 160u

typedef enum { VIEW_RELEASE = 0, VIEW_INBOX = 1 } store_view_t;
typedef enum {
    APP_ACTION_INSTALL = 0,
    APP_ACTION_UPDATE = 1,
    APP_ACTION_CURRENT = 2,
} app_action_t;

static t5_ui_list_row_t rows[MAX_CATALOG_ITEMS];
static char row_titles[MAX_CATALOG_ITEMS][TITLE_MAX];
static char row_subtitles[MAX_CATALOG_ITEMS][SUBTITLE_MAX];
static char row_values[MAX_CATALOG_ITEMS][VALUE_MAX];
static char row_folders[MAX_CATALOG_ITEMS][T5_PACKAGE_ID_MAX];
static t5_package_preview_t inbox_packages[MAX_CATALOG_ITEMS];
static uint32_t row_catalog_index[MAX_CATALOG_ITEMS];
static uint32_t row_count;
static store_view_t view;

static bool has_catalog_api(const t5_app_api_v1 *api) {
    const size_t required = offsetof(t5_app_api_v1, app_catalog_download) + sizeof(api->app_catalog_download);
    return api && api->struct_size >= required && api->app_catalog_refresh && api->app_catalog_count &&
           api->app_catalog_get && api->app_catalog_download;
}
static bool has_catalog_manifest_api(const t5_app_api_v1 *api) {
    const size_t required = offsetof(t5_app_api_v1, app_catalog_manifest_get) + sizeof(api->app_catalog_manifest_get);
    return api && api->struct_size >= required && api->app_catalog_manifest_get;
}
static bool has_version_api(const t5_app_api_v1 *api) {
    const size_t required = offsetof(t5_app_api_v1, app_catalog_version_get) + sizeof(api->app_catalog_version_get);
    return api && api->struct_size >= required && api->installed_app_version_get && api->app_catalog_version_get;
}
static bool has_package_api(const t5_package_manager_api_v1 *manager) {
    return manager && manager->api_version == T5_PACKAGE_MANAGER_API_VERSION &&
           manager->struct_size >= sizeof(t5_package_manager_api_v1) &&
           manager->preview && manager->install && manager->uninstall;
}
static bool has_ui_api(const t5_ui_api_v1 *ui) {
    const size_t required = offsetof(t5_ui_api_v1, previous_index) + sizeof(ui->previous_index);
    return ui && ui->struct_size >= required && ui->render_list && ui->hit_test && ui->poll_event &&
           ui->next_index && ui->previous_index;
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
    if (!api->installed_app_version_get(manifest->file_name, installed_version, installed_capacity))
        return APP_ACTION_INSTALL;
    if (!strcmp(available_version, installed_version)) return APP_ACTION_CURRENT;
    return APP_ACTION_UPDATE;
}
static void build_release_rows(const t5_app_api_v1 *api) {
    uint32_t count = api->app_catalog_count();
    if (count > MAX_CATALOG_ITEMS) count = MAX_CATALOG_ITEMS;
    row_count = 0;
    for (uint32_t catalog_index = 0; catalog_index < count; ++catalog_index) {
        t5_app_release_asset_t asset = {0};
        t5_app_manifest_t manifest = {0};
        bool has_manifest = false;
        const char *name = catalog_display_name(api, catalog_index, &asset, &manifest, &has_manifest);
        if (!name) continue;
        char available[T5_APP_VERSION_MAX] = {0};
        char installed[T5_APP_VERSION_MAX] = {0};
        const app_action_t action = has_manifest ? catalog_action(api, catalog_index, &manifest,
                available, sizeof(available), installed, sizeof(installed)) : APP_ACTION_INSTALL;
        snprintf(row_titles[row_count], sizeof(row_titles[row_count]), "%s", name);
        if (has_manifest && !manifest.compatible) {
            snprintf(row_subtitles[row_count], sizeof(row_subtitles[row_count]), "Requires newer firmware");
            snprintf(row_values[row_count], sizeof(row_values[row_count]), "%s", available);
        } else if (action == APP_ACTION_CURRENT) {
            snprintf(row_subtitles[row_count], sizeof(row_subtitles[row_count]), "Installed");
            snprintf(row_values[row_count], sizeof(row_values[row_count]), "%s", installed[0] ? installed : available);
        } else if (action == APP_ACTION_UPDATE) {
            snprintf(row_subtitles[row_count], sizeof(row_subtitles[row_count]),
                     installed[0] ? "Installed %s" : "Update available", installed);
            snprintf(row_values[row_count], sizeof(row_values[row_count]), "%s", available[0] ? available : "Update");
        } else {
            snprintf(row_subtitles[row_count], sizeof(row_subtitles[row_count]), "Not installed");
            snprintf(row_values[row_count], sizeof(row_values[row_count]), "%s", available[0] ? available : "Install");
        }
        rows[row_count] = (t5_ui_list_row_t){row_titles[row_count], row_subtitles[row_count],
                                            row_values[row_count],
                                            action == APP_ACTION_UPDATE ? T5_UI_LIST_HIGHLIGHT_VALUE : 0};
        row_catalog_index[row_count] = catalog_index;
        ++row_count;
    }
}

// Use the same unified preview/install/uninstall path as Package Manager.
// No other package kind is ever exposed by this App Store view. A directory
// iterator is closed before any transaction changes storage contents.
static bool build_inbox_rows(const t5_app_api_v1 *app,
                             const t5_package_manager_api_v1 *manager) {
    row_count = 0;
    if (!app->dir_open("/sd/Packages/Inbox")) return false;
    t5_app_dirent_t entry = {0};
    while (app->dir_next(&entry)) {
        if (!entry.is_directory || row_count >= MAX_CATALOG_ITEMS) continue;
        t5_package_preview_t info = {0};
        if (!manager->preview(entry.name, &info) || info.kind != T5_PACKAGE_APPLICATION) continue;
        snprintf(row_folders[row_count], sizeof(row_folders[row_count]), "%s", entry.name);
        inbox_packages[row_count] = info;
        snprintf(row_titles[row_count], sizeof(row_titles[row_count]), "%s", info.id);
        if (!info.valid_installation)
            snprintf(row_subtitles[row_count], sizeof(row_subtitles[row_count]), "Installed package needs recovery");
        else if (info.installed_version[0])
            snprintf(row_subtitles[row_count], sizeof(row_subtitles[row_count]),
                     "Installed %s; %s", info.installed_version,
                     info.install_allowed ? "update available" : "no update");
        else
            snprintf(row_subtitles[row_count], sizeof(row_subtitles[row_count]), "%s",
                     info.install_allowed ? "SD inbox: ready to install" : "Dependency, version or stage blocked");
        snprintf(row_values[row_count], sizeof(row_values[row_count]), "%s", info.version);
        rows[row_count] = (t5_ui_list_row_t){row_titles[row_count], row_subtitles[row_count],
                                            row_values[row_count],
                                            info.install_allowed ? T5_UI_LIST_HIGHLIGHT_VALUE : 0};
        ++row_count;
    }
    app->dir_close();
    return true;
}
static app_action_t selected_release_action(const t5_app_api_v1 *api, int32_t selected,
                                            t5_app_manifest_t *manifest_out, bool *has_manifest_out) {
    if (manifest_out) *manifest_out = (t5_app_manifest_t){0};
    if (has_manifest_out) *has_manifest_out = false;
    if (selected < 0 || selected >= (int32_t)row_count) return APP_ACTION_INSTALL;
    const uint32_t index = row_catalog_index[selected];
    t5_app_release_asset_t asset = {0};
    t5_app_manifest_t manifest = {0};
    bool has_manifest = false;
    (void)catalog_display_name(api, index, &asset, &manifest, &has_manifest);
    if (manifest_out) *manifest_out = manifest;
    if (has_manifest_out) *has_manifest_out = has_manifest;
    char available[T5_APP_VERSION_MAX] = {0}, installed[T5_APP_VERSION_MAX] = {0};
    return has_manifest ? catalog_action(api, index, &manifest,
            available, sizeof(available), installed, sizeof(installed)) : APP_ACTION_INSTALL;
}
static const char *confirm_label(const t5_app_api_v1 *api, int32_t selected) {
    if (selected < 0 || selected >= (int32_t)row_count) return "";
    if (view == VIEW_INBOX) {
        const t5_package_preview_t *info = &inbox_packages[selected];
        if (!info->valid_installation) return "";
        if (info->install_allowed) return info->installed_version[0] ? "Update" : "Install";
        return info->installed_version[0] ? "Actions" : "";
    }
    t5_app_manifest_t manifest = {0};
    bool has_manifest = false;
    const app_action_t action = selected_release_action(api, selected, &manifest, &has_manifest);
    if (has_manifest && !manifest.compatible) return "";
    if (action == APP_ACTION_CURRENT) return "";
    return action == APP_ACTION_UPDATE ? "Update" : "Install";
}
static void render_rows(const t5_app_api_v1 *api, const t5_ui_api_v1 *ui,
                        int32_t selected, const char *status) {
    const t5_ui_chrome_t chrome = {
        .title = "App Store",
        .subtitle = view == VIEW_INBOX ? "SD inbox apps; tap header for releases" :
                   "Latest release; tap header for SD packages",
        .status = status ? status : "",
        .back_label = "Back",
        .confirm_label = confirm_label(api, selected),
        .previous_label = "Up", .next_label = "Down",
    };
    if (row_count) ui->render_list(&chrome, rows, row_count, selected);
    else {
        const t5_ui_list_row_t empty = {"No applications", view == VIEW_INBOX ?
            "Add an application to /Packages/Inbox/<id>" : "Release catalog is empty", "", 0};
        ui->render_list(&chrome, &empty, 1, 0);
    }
}
static bool refresh_release(const t5_app_api_v1 *api, const t5_ui_api_v1 *ui) {
    const t5_ui_list_row_t loading = {"Loading release catalog", "Using saved Wi-Fi", "", 0};
    const t5_ui_chrome_t chrome = {"App Store", "Latest release apps", "Connecting with saved Wi-Fi...",
                                   "Back", "", "", ""};
    ui->render_list(&chrome, &loading, 1, 0);
    if (!api->app_catalog_refresh()) return false;
    build_release_rows(api);
    return true;
}
static bool retry_or_exit(const t5_ui_api_v1 *ui) {
    const t5_ui_list_row_t retry = {"Retry catalog refresh", "Unable to load latest release", "Retry",
                                     T5_UI_LIST_HIGHLIGHT_VALUE};
    const t5_ui_chrome_t chrome = {"App Store", "Latest release apps", "Check saved Wi-Fi, then retry",
                                   "Back", "Retry", "", ""};
    ui->render_list(&chrome, &retry, 1, 0);
    for (;;) {
        t5_ui_event_t event = {0};
        if (!ui->poll_event(&event, 20)) return false;
        if (event.type == T5_UI_EVENT_CONFIRM ||
            (event.type == T5_UI_EVENT_TAP && ui->hit_test(event.touch_x, event.touch_y) == 0)) return true;
        if (event.type == T5_UI_EVENT_BACK || event.type == T5_UI_EVENT_EXIT) return false;
    }
}
static void activate_release(const t5_app_api_v1 *api, const t5_ui_api_v1 *ui,
                             int32_t selected, char *status, size_t capacity) {
    if (selected < 0 || selected >= (int32_t)row_count) return;
    const uint32_t index = row_catalog_index[selected];
    t5_app_release_asset_t asset = {0};
    t5_app_manifest_t manifest = {0};
    bool has_manifest = false;
    const char *name_ptr = catalog_display_name(api, index, &asset, &manifest, &has_manifest);
    if (!name_ptr) return;
    char name[TITLE_MAX];
    snprintf(name, sizeof(name), "%s", name_ptr);
    char available[T5_APP_VERSION_MAX] = {0}, installed[T5_APP_VERSION_MAX] = {0};
    const app_action_t action = has_manifest ? catalog_action(api, index, &manifest,
            available, sizeof(available), installed, sizeof(installed)) : APP_ACTION_INSTALL;
    if (has_manifest && !manifest.compatible) {
        snprintf(status, capacity, "%s requires newer firmware", name);
        return;
    }
    if (action == APP_ACTION_CURRENT) {
        snprintf(status, capacity, "%s is already current", name);
        return;
    }
    snprintf(status, capacity, "%s %s...", action == APP_ACTION_UPDATE ? "Updating" : "Installing", name);
    render_rows(api, ui, selected, status);
    const bool ok = api->app_catalog_download(index);
    build_release_rows(api);
    snprintf(status, capacity, "%s %s", name, ok ? "installed" : "installation failed");
}
static bool confirm_removal(const t5_ui_api_v1 *ui, const char *id) {
    const t5_ui_list_row_t choices[2] = {
        {"Keep application", "Leave installed files unchanged", "Cancel", 0},
        {"Uninstall application", "Requires inactive ELF; irreversible", "Remove", T5_UI_LIST_HIGHLIGHT_VALUE},
    };
    const t5_ui_chrome_t chrome = {"Confirm uninstall", id, "Select Remove and Confirm",
                                   "Cancel", "Select", "Up", "Down"};
    int32_t selected = 0;
    ui->render_list(&chrome, choices, 2, selected);
    for (;;) {
        t5_ui_event_t event = {0};
        if (!ui->poll_event(&event, 20) || event.type == T5_UI_EVENT_BACK ||
            event.type == T5_UI_EVENT_EXIT) return false;
        if (event.type == T5_UI_EVENT_PREVIOUS) selected = ui->previous_index(selected, 2);
        if (event.type == T5_UI_EVENT_NEXT) selected = ui->next_index(selected, 2);
        if (event.type == T5_UI_EVENT_TAP) {
            const int32_t hit = ui->hit_test(event.touch_x, event.touch_y);
            if (hit < 0 || hit >= 2) continue;
            selected = hit;
        }
        if (event.type == T5_UI_EVENT_CONFIRM) return selected == 1;
        ui->render_list(&chrome, choices, 2, selected);
    }
}
static void activate_inbox(const t5_package_manager_api_v1 *manager, const t5_ui_api_v1 *ui,
                           int32_t selected, char *status, size_t capacity) {
    if (selected < 0 || selected >= (int32_t)row_count) return;
    const t5_package_preview_t info = inbox_packages[selected];
    if (!info.valid_installation) {
        snprintf(status, capacity, "%s: recovery required", info.id);
        return;
    }
    if (info.install_allowed) {
        const bool ok = manager->install(row_folders[selected]);
        snprintf(status, capacity, "%s: %s", info.id,
                 ok ? "verified package installed" : "install refused; inspect stage/dependencies");
    } else if (info.installed_version[0] && confirm_removal(ui, info.id)) {
        const bool ok = manager->uninstall(T5_PACKAGE_APPLICATION, info.id);
        snprintf(status, capacity, "%s: %s", info.id, ok ? "uninstalled" : "uninstall refused; stop active users");
    } else if (!info.installed_version[0]) {
        snprintf(status, capacity, "%s: install blocked; check dependencies or stage", info.id);
    }
}

__attribute__((visibility("default"))) void app_main(void) {
    const t5_app_api_v1 *api = t5_app_get_api(T5_APP_ABI_VERSION);
    const t5_package_manager_api_v1 *manager = t5_package_manager_get_api(T5_PACKAGE_MANAGER_API_VERSION);
    const t5_ui_api_v1 *ui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!has_catalog_api(api) || !has_ui_api(ui) || !api->set_back_exits_app ||
        !api->dir_open || !api->dir_next || !api->dir_close || !has_package_api(manager)) return;
    api->set_back_exits_app(false);
    view = VIEW_RELEASE;
    if (!refresh_release(api, ui)) {
        while (retry_or_exit(ui)) {
            if (refresh_release(api, ui)) break;
        }
        // Offline packages remain usable even when the network is unavailable.
        if (!api->app_catalog_count()) {
            view = VIEW_INBOX;
            (void)build_inbox_rows(api, manager);
        }
    }
    int32_t selected = 0;
    char status[STATUS_MAX] = {0};
    render_rows(api, ui, selected, status);
    for (;;) {
        t5_ui_event_t event = {0};
        if (!ui->poll_event(&event, 20)) break;
        bool redraw = false;
        switch (event.type) {
            case T5_UI_EVENT_PREVIOUS:
                selected = ui->previous_index(selected, row_count);
                status[0] = '\0'; redraw = true; break;
            case T5_UI_EVENT_NEXT:
                selected = ui->next_index(selected, row_count);
                status[0] = '\0'; redraw = true; break;
            case T5_UI_EVENT_CONFIRM:
                if (view == VIEW_INBOX) {
                    activate_inbox(manager, ui, selected, status, sizeof(status));
                    (void)build_inbox_rows(api, manager);
                } else activate_release(api, ui, selected, status, sizeof(status));
                redraw = true; break;
            case T5_UI_EVENT_TAP: {
                const int32_t hit = ui->hit_test(event.touch_x, event.touch_y);
                if (hit == T5_UI_HIT_HEADER) {
                    view = view == VIEW_RELEASE ? VIEW_INBOX : VIEW_RELEASE;
                    if (view == VIEW_INBOX) (void)build_inbox_rows(api, manager);
                    else if (!refresh_release(api, ui)) snprintf(status, sizeof(status), "Release refresh failed; tap header for SD apps");
                    else status[0] = '\0';
                    selected = 0;
                    redraw = true;
                } else if (hit >= 0 && hit < (int32_t)row_count) {
                    if (hit == selected) {
                        if (view == VIEW_INBOX) {
                            activate_inbox(manager, ui, selected, status, sizeof(status));
                            (void)build_inbox_rows(api, manager);
                        } else activate_release(api, ui, selected, status, sizeof(status));
                    } else { selected = hit; status[0] = '\0'; }
                    redraw = true;
                }
                break;
            }
            case T5_UI_EVENT_BACK:
            case T5_UI_EVENT_EXIT:
                api->set_back_exits_app(true);
                return;
            default: break;
        }
        if (!row_count || selected >= (int32_t)row_count) selected = 0;
        if (redraw) render_rows(api, ui, selected, status);
    }
    api->set_back_exits_app(true);
}
