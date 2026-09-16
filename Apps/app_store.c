#include "T5AppApi.h"
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

typedef enum {
    APP_ACTION_INSTALL = 0,
    APP_ACTION_UPDATE = 1,
    APP_ACTION_CURRENT = 2,
} app_action_t;

static t5_ui_list_row_t rows[MAX_CATALOG_ITEMS];
static char row_titles[MAX_CATALOG_ITEMS][TITLE_MAX];
static char row_subtitles[MAX_CATALOG_ITEMS][SUBTITLE_MAX];
static char row_values[MAX_CATALOG_ITEMS][VALUE_MAX];
static uint32_t row_catalog_index[MAX_CATALOG_ITEMS];
static uint32_t row_count;

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
    if (!api->installed_app_version_get(manifest->file_name, installed_version, installed_capacity)) {
        return APP_ACTION_INSTALL;
    }
    if (!strcmp(available_version, installed_version)) return APP_ACTION_CURRENT;
    return APP_ACTION_UPDATE;
}

static void build_rows(const t5_app_api_v1 *api) {
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
        const app_action_t action = has_manifest
            ? catalog_action(api, catalog_index, &manifest, available, sizeof(available), installed, sizeof(installed))
            : APP_ACTION_INSTALL;

        snprintf(row_titles[row_count], sizeof(row_titles[row_count]), "%s", name);
        row_values[row_count][0] = '\0';

        if (has_manifest && !manifest.compatible) {
            snprintf(row_subtitles[row_count], sizeof(row_subtitles[row_count]), "Requires newer firmware");
            snprintf(row_values[row_count], sizeof(row_values[row_count]), "%s", available);
        } else if (action == APP_ACTION_CURRENT) {
            snprintf(row_subtitles[row_count], sizeof(row_subtitles[row_count]), "Installed");
            snprintf(row_values[row_count], sizeof(row_values[row_count]), "%s", installed[0] ? installed : available);
        } else if (action == APP_ACTION_UPDATE) {
            if (installed[0])
                snprintf(row_subtitles[row_count], sizeof(row_subtitles[row_count]), "Installed %s", installed);
            else
                snprintf(row_subtitles[row_count], sizeof(row_subtitles[row_count]), "Update available");
            snprintf(row_values[row_count], sizeof(row_values[row_count]), "%s", available[0] ? available : "Update");
        } else {
            snprintf(row_subtitles[row_count], sizeof(row_subtitles[row_count]), "Not installed");
            snprintf(row_values[row_count], sizeof(row_values[row_count]), "%s", available[0] ? available : "Install");
        }

        rows[row_count].title = row_titles[row_count];
        rows[row_count].subtitle = row_subtitles[row_count];
        rows[row_count].value = row_values[row_count];
        rows[row_count].flags = action == APP_ACTION_UPDATE ? T5_UI_LIST_HIGHLIGHT_VALUE : 0;
        row_catalog_index[row_count] = catalog_index;
        ++row_count;
    }
}

static app_action_t selected_action(const t5_app_api_v1 *api, int32_t selected,
                                    t5_app_manifest_t *manifest_out, bool *has_manifest_out,
                                    char *available, size_t available_capacity,
                                    char *installed, size_t installed_capacity) {
    if (manifest_out) *manifest_out = (t5_app_manifest_t){0};
    if (has_manifest_out) *has_manifest_out = false;
    if (available && available_capacity) available[0] = '\0';
    if (installed && installed_capacity) installed[0] = '\0';
    if (selected < 0 || selected >= (int32_t)row_count) return APP_ACTION_INSTALL;

    const uint32_t catalog_index = row_catalog_index[selected];
    t5_app_release_asset_t asset = {0};
    t5_app_manifest_t manifest = {0};
    bool has_manifest = false;
    (void)catalog_display_name(api, catalog_index, &asset, &manifest, &has_manifest);
    if (manifest_out) *manifest_out = manifest;
    if (has_manifest_out) *has_manifest_out = has_manifest;
    return has_manifest
        ? catalog_action(api, catalog_index, &manifest, available, available_capacity, installed, installed_capacity)
        : APP_ACTION_INSTALL;
}

static const char *confirm_label(const t5_app_api_v1 *api, int32_t selected) {
    if (selected < 0 || selected >= (int32_t)row_count) return "";
    t5_app_manifest_t manifest = {0};
    bool has_manifest = false;
    char available[T5_APP_VERSION_MAX] = {0};
    char installed[T5_APP_VERSION_MAX] = {0};
    const app_action_t action = selected_action(api, selected, &manifest, &has_manifest,
                                                available, sizeof(available), installed, sizeof(installed));
    if (has_manifest && !manifest.compatible) return "";
    if (action == APP_ACTION_CURRENT) return "";
    return action == APP_ACTION_UPDATE ? "Update" : "Install";
}

static void render_rows(const t5_app_api_v1 *api, const t5_ui_api_v1 *ui,
                        int32_t selected, const char *status) {
    const t5_ui_chrome_t chrome = {
        .title = "App Store",
        .subtitle = "Latest release apps",
        .status = status ? status : "",
        .back_label = "Back",
        .confirm_label = confirm_label(api, selected),
        .previous_label = "Up",
        .next_label = "Down",
    };
    ui->render_list(&chrome, rows, row_count, selected);
}

static void render_loading(const t5_ui_api_v1 *ui, const char *status) {
    const t5_ui_list_row_t loading = {
        .title = "Loading release catalog",
        .subtitle = "Using saved Wi-Fi",
        .value = "",
        .flags = 0,
    };
    const t5_ui_chrome_t chrome = {
        .title = "App Store",
        .subtitle = "Latest release apps",
        .status = status ? status : "",
        .back_label = "Back",
        .confirm_label = "",
        .previous_label = "",
        .next_label = "",
    };
    ui->render_list(&chrome, &loading, 1, 0);
}

static bool refresh_catalog(const t5_app_api_v1 *api, const t5_ui_api_v1 *ui) {
    render_loading(ui, "Connecting with saved Wi-Fi...");
    if (!api->app_catalog_refresh()) return false;
    build_rows(api);
    return true;
}

static bool wait_for_retry_or_exit(const t5_ui_api_v1 *ui) {
    const t5_ui_list_row_t retry = {
        .title = "Retry catalog refresh",
        .subtitle = "Unable to load latest release",
        .value = "Retry",
        .flags = T5_UI_LIST_HIGHLIGHT_VALUE,
    };
    const t5_ui_chrome_t chrome = {
        .title = "App Store",
        .subtitle = "Latest release apps",
        .status = "Check saved Wi-Fi, then retry",
        .back_label = "Back",
        .confirm_label = "Retry",
        .previous_label = "",
        .next_label = "",
    };
    ui->render_list(&chrome, &retry, 1, 0);

    for (;;) {
        t5_ui_event_t event = {0};
        if (!ui->poll_event(&event, 20)) return false;
        if (event.type == T5_UI_EVENT_CONFIRM) return true;
        if (event.type == T5_UI_EVENT_TAP && ui->hit_test(event.touch_x, event.touch_y) == 0) return true;
        if (event.type == T5_UI_EVENT_BACK || event.type == T5_UI_EVENT_EXIT) return false;
    }
}

static void activate_selected(const t5_app_api_v1 *api, const t5_ui_api_v1 *ui,
                              int32_t selected, char *status, size_t status_capacity) {
    if (!status || status_capacity == 0 || selected < 0 || selected >= (int32_t)row_count) return;

    const uint32_t catalog_index = row_catalog_index[selected];
    t5_app_release_asset_t asset = {0};
    t5_app_manifest_t manifest = {0};
    bool has_manifest = false;
    const char *name_ptr = catalog_display_name(api, catalog_index, &asset, &manifest, &has_manifest);
    if (!name_ptr) return;

    char name[TITLE_MAX];
    snprintf(name, sizeof(name), "%s", name_ptr);
    char available[T5_APP_VERSION_MAX] = {0};
    char installed[T5_APP_VERSION_MAX] = {0};
    const app_action_t action = has_manifest
        ? catalog_action(api, catalog_index, &manifest, available, sizeof(available), installed, sizeof(installed))
        : APP_ACTION_INSTALL;

    if (has_manifest && !manifest.compatible) {
        snprintf(status, status_capacity, "%s requires newer firmware", name);
        render_rows(api, ui, selected, status);
        return;
    }
    if (action == APP_ACTION_CURRENT) {
        snprintf(status, status_capacity, "%s is already current", name);
        render_rows(api, ui, selected, status);
        return;
    }

    snprintf(status, status_capacity, "%s %s...", action == APP_ACTION_UPDATE ? "Updating" : "Installing", name);
    render_rows(api, ui, selected, status);

    const bool ok = api->app_catalog_download(catalog_index);
    build_rows(api);
    if (ok)
        snprintf(status, status_capacity, "%s %s", name, action == APP_ACTION_UPDATE ? "updated" : "installed");
    else
        snprintf(status, status_capacity, "%s %s failed", name, action == APP_ACTION_UPDATE ? "update" : "install");
}

__attribute__((visibility("default"))) void app_main(void) {
    const t5_app_api_v1 *api = t5_app_get_api(T5_APP_ABI_VERSION);
    const t5_ui_api_v1 *ui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!has_catalog_api(api) || !has_ui_api(ui) || !api->set_back_exits_app) return;

    api->set_back_exits_app(false);

    bool loaded = refresh_catalog(api, ui);
    while (!loaded) {
        if (!wait_for_retry_or_exit(ui)) {
            api->set_back_exits_app(true);
            return;
        }
        loaded = refresh_catalog(api, ui);
    }

    int32_t selected = 0;
    char status[STATUS_MAX] = {0};
    if (row_count == 0) snprintf(status, sizeof(status), "No release apps found");
    render_rows(api, ui, selected, status);

    for (;;) {
        t5_ui_event_t event = {0};
        if (!ui->poll_event(&event, 20)) break;

        bool redraw = false;
        switch (event.type) {
            case T5_UI_EVENT_PREVIOUS:
                selected = ui->previous_index(selected, row_count);
                status[0] = '\0';
                redraw = true;
                break;
            case T5_UI_EVENT_NEXT:
                selected = ui->next_index(selected, row_count);
                status[0] = '\0';
                redraw = true;
                break;
            case T5_UI_EVENT_CONFIRM:
                activate_selected(api, ui, selected, status, sizeof(status));
                redraw = true;
                break;
            case T5_UI_EVENT_TAP: {
                const int32_t hit = ui->hit_test(event.touch_x, event.touch_y);
                if (hit >= 0 && hit < (int32_t)row_count) {
                    if (hit == selected) activate_selected(api, ui, selected, status, sizeof(status));
                    else {
                        selected = hit;
                        status[0] = '\0';
                    }
                    redraw = true;
                } else if (hit == T5_UI_HIT_HEADER) {
                    loaded = refresh_catalog(api, ui);
                    if (!loaded) {
                        while (!loaded) {
                            if (!wait_for_retry_or_exit(ui)) {
                                api->set_back_exits_app(true);
                                return;
                            }
                            loaded = refresh_catalog(api, ui);
                        }
                    }
                    selected = 0;
                    status[0] = '\0';
                    redraw = true;
                }
                break;
            }
            case T5_UI_EVENT_BACK:
            case T5_UI_EVENT_EXIT:
                api->set_back_exits_app(true);
                return;
            default:
                break;
        }

        if (row_count == 0) selected = 0;
        else if (selected >= (int32_t)row_count) selected = (int32_t)row_count - 1;
        if (redraw) render_rows(api, ui, selected, status);
    }

    api->set_back_exits_app(true);
}
