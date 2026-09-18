#include "T5AppApi.h"
#include "T5DriverManagerApi.h"
#include "T5PackageVersion.h"
#include "T5UiApi.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_DRIVER_ITEMS 64u
#define TITLE_MAX 80u
#define SUBTITLE_MAX 96u
#define VALUE_MAX 40u
#define STATUS_MAX 160u

typedef enum {
    DRIVER_ACTION_INSTALL = 0,
    DRIVER_ACTION_UPDATE = 1,
    DRIVER_ACTION_CURRENT = 2,
    DRIVER_ACTION_INSTALLED_NEWER = 3,
    DRIVER_ACTION_UNAVAILABLE = 4,
} driver_action_t;

static t5_driver_catalog_entry_t entries[MAX_DRIVER_ITEMS];
static t5_ui_list_row_t rows[MAX_DRIVER_ITEMS];
static char row_titles[MAX_DRIVER_ITEMS][TITLE_MAX];
static char row_subtitles[MAX_DRIVER_ITEMS][SUBTITLE_MAX];
static char row_values[MAX_DRIVER_ITEMS][VALUE_MAX];
static uint32_t row_count;

static bool has_driver_api(const t5_driver_manager_api_v1 *api) {
    const size_t required = offsetof(t5_driver_manager_api_v1, install) + sizeof(api->install);
    return api && api->api_version == T5_DRIVER_MANAGER_API_VERSION && api->struct_size >= required &&
           api->catalog_refresh && api->catalog_count && api->catalog_get &&
           api->installed_version_get && api->install;
}

static bool has_ui_api(const t5_ui_api_v1 *ui) {
    const size_t required = offsetof(t5_ui_api_v1, previous_index) + sizeof(ui->previous_index);
    return ui && ui->struct_size >= required && ui->render_list && ui->hit_test && ui->poll_event &&
           ui->next_index && ui->previous_index;
}

static driver_action_t action_for(const t5_driver_manager_api_v1 *api, uint32_t index,
                                  char *installed, size_t installed_capacity) {
    if (installed && installed_capacity) installed[0] = '\0';
    // build_rows calls this BEFORE incrementing row_count. Validate the array
    // bound here; the user-interaction callers independently check row_count.
    if (index >= MAX_DRIVER_ITEMS || !installed || !installed_capacity) return DRIVER_ACTION_UNAVAILABLE;
    if (!api->installed_version_get(entries[index].id, installed, installed_capacity)) {
        return DRIVER_ACTION_INSTALL;
    }
    switch (t5_package_version_compare(entries[index].version, installed)) {
        case 1: return DRIVER_ACTION_UPDATE;
        case 0: return DRIVER_ACTION_CURRENT;
        case -1: return DRIVER_ACTION_INSTALLED_NEWER;
        default: return DRIVER_ACTION_UNAVAILABLE;
    }
}

static void build_rows(const t5_driver_manager_api_v1 *api) {
    uint32_t count = api->catalog_count();
    if (count > MAX_DRIVER_ITEMS) count = MAX_DRIVER_ITEMS;
    row_count = 0;

    for (uint32_t i = 0; i < count; ++i) {
        t5_driver_catalog_entry_t entry = {0};
        if (!api->catalog_get(i, &entry) || !entry.id[0] || !entry.version[0]) continue;
        entries[row_count] = entry;

        char installed[T5_DRIVER_VERSION_MAX] = {0};
        const driver_action_t action = action_for(api, row_count, installed, sizeof(installed));
        snprintf(row_titles[row_count], sizeof(row_titles[row_count]), "%s", entry.id);
        if (action == DRIVER_ACTION_CURRENT) {
            snprintf(row_subtitles[row_count], sizeof(row_subtitles[row_count]),
                     "%s - Installed", entry.capability[0] ? entry.capability : "Driver package");
        } else if (action == DRIVER_ACTION_INSTALLED_NEWER) {
            snprintf(row_subtitles[row_count], sizeof(row_subtitles[row_count]),
                     "Installed newer %s", installed);
        } else if (action == DRIVER_ACTION_UNAVAILABLE) {
            snprintf(row_subtitles[row_count], sizeof(row_subtitles[row_count]),
                     "Installed version invalid; inspect package");
        } else if (action == DRIVER_ACTION_UPDATE) {
            snprintf(row_subtitles[row_count], sizeof(row_subtitles[row_count]),
                     "%s - Installed %s", entry.capability[0] ? entry.capability : "Driver package", installed);
        } else {
            snprintf(row_subtitles[row_count], sizeof(row_subtitles[row_count]),
                     "%s - Not installed", entry.capability[0] ? entry.capability : "Driver package");
        }
        snprintf(row_values[row_count], sizeof(row_values[row_count]), "%s", entry.version);
        rows[row_count].title = row_titles[row_count];
        rows[row_count].subtitle = row_subtitles[row_count];
        rows[row_count].value = row_values[row_count];
        rows[row_count].flags = action == DRIVER_ACTION_UPDATE ? T5_UI_LIST_HIGHLIGHT_VALUE : 0;
        ++row_count;
    }
}

static const char *confirm_label(const t5_driver_manager_api_v1 *api, int32_t selected) {
    if (selected < 0 || selected >= (int32_t)row_count) return "";
    char installed[T5_DRIVER_VERSION_MAX] = {0};
    const driver_action_t action = action_for(api, (uint32_t)selected, installed, sizeof(installed));
    if (action == DRIVER_ACTION_UPDATE) return "Update";
    if (action == DRIVER_ACTION_INSTALL) return "Install";
    return "";
}

static void render_rows(const t5_driver_manager_api_v1 *api, const t5_ui_api_v1 *ui,
                        int32_t selected, const char *status) {
    const t5_ui_chrome_t chrome = {
        .title = "Driver Manager",
        .subtitle = "GitHub release drivers",
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
        .title = "Loading driver catalog",
        .subtitle = "Using saved Wi-Fi",
        .value = "",
        .flags = 0,
    };
    const t5_ui_chrome_t chrome = {
        .title = "Driver Manager",
        .subtitle = "GitHub release drivers",
        .status = status ? status : "",
        .back_label = "Back",
        .confirm_label = "",
        .previous_label = "",
        .next_label = "",
    };
    ui->render_list(&chrome, &loading, 1, 0);
}

static bool refresh_catalog(const t5_driver_manager_api_v1 *api, const t5_ui_api_v1 *ui) {
    render_loading(ui, "Connecting with saved Wi-Fi...");
    if (!api->catalog_refresh()) return false;
    build_rows(api);
    return true;
}

static bool wait_for_retry_or_exit(const t5_ui_api_v1 *ui) {
    const t5_ui_list_row_t retry = {
        .title = "Retry driver refresh",
        .subtitle = "Unable to load release drivers",
        .value = "Retry",
        .flags = T5_UI_LIST_HIGHLIGHT_VALUE,
    };
    const t5_ui_chrome_t chrome = {
        .title = "Driver Manager",
        .subtitle = "GitHub release drivers",
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

static void activate_selected(const t5_driver_manager_api_v1 *api, const t5_ui_api_v1 *ui,
                              int32_t selected, char *status, size_t status_capacity) {
    if (!status || !status_capacity || selected < 0 || selected >= (int32_t)row_count) return;
    char installed[T5_DRIVER_VERSION_MAX] = {0};
    const driver_action_t action = action_for(api, (uint32_t)selected, installed, sizeof(installed));
    if (action == DRIVER_ACTION_CURRENT) {
        snprintf(status, status_capacity, "%s is already current", entries[selected].id);
        return;
    }
    if (action == DRIVER_ACTION_INSTALLED_NEWER) {
        snprintf(status, status_capacity, "%s: installed %s is newer than catalog", entries[selected].id, installed);
        return;
    }
    if (action == DRIVER_ACTION_UNAVAILABLE) {
        snprintf(status, status_capacity, "%s: version invalid; inspect installation", entries[selected].id);
        return;
    }

    snprintf(status, status_capacity, "%s %s...",
             action == DRIVER_ACTION_UPDATE ? "Updating" : "Installing", entries[selected].id);
    render_rows(api, ui, selected, status);

    const bool ok = api->install((uint32_t)selected);
    build_rows(api);
    if (ok) {
        snprintf(status, status_capacity, "%s %s; activation unchanged", entries[selected].id,
                 action == DRIVER_ACTION_UPDATE ? "updated" : "installed");
    } else {
        snprintf(status, status_capacity, "%s %s failed", entries[selected].id,
                 action == DRIVER_ACTION_UPDATE ? "update" : "install");
    }
}

__attribute__((visibility("default"))) void app_main(void) {
    const t5_app_api_v1 *app = t5_app_get_api(T5_APP_ABI_VERSION);
    const t5_driver_manager_api_v1 *drivers = t5_driver_manager_get_api(T5_DRIVER_MANAGER_API_VERSION);
    const t5_ui_api_v1 *ui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!app || !app->set_back_exits_app || !has_driver_api(drivers) || !has_ui_api(ui)) return;

    app->set_back_exits_app(false);
    bool loaded = refresh_catalog(drivers, ui);
    while (!loaded) {
        if (!wait_for_retry_or_exit(ui)) {
            app->set_back_exits_app(true);
            return;
        }
        loaded = refresh_catalog(drivers, ui);
    }

    int32_t selected = 0;
    char status[STATUS_MAX] = {0};
    if (row_count == 0) snprintf(status, sizeof(status), "No driver packages found in latest release");
    render_rows(drivers, ui, selected, status);

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
                activate_selected(drivers, ui, selected, status, sizeof(status));
                redraw = true;
                break;
            case T5_UI_EVENT_TAP: {
                const int32_t hit = ui->hit_test(event.touch_x, event.touch_y);
                if (hit >= 0 && hit < (int32_t)row_count) {
                    if (hit == selected) activate_selected(drivers, ui, selected, status, sizeof(status));
                    else {
                        selected = hit;
                        status[0] = '\0';
                    }
                    redraw = true;
                } else if (hit == T5_UI_HIT_HEADER) {
                    loaded = refresh_catalog(drivers, ui);
                    while (!loaded) {
                        if (!wait_for_retry_or_exit(ui)) {
                            app->set_back_exits_app(true);
                            return;
                        }
                        loaded = refresh_catalog(drivers, ui);
                    }
                    selected = 0;
                    status[0] = '\0';
                    redraw = true;
                }
                break;
            }
            case T5_UI_EVENT_BACK:
            case T5_UI_EVENT_EXIT:
                app->set_back_exits_app(true);
                return;
            default:
                break;
        }
        if (row_count == 0) selected = 0;
        else if (selected >= (int32_t)row_count) selected = (int32_t)row_count - 1;
        if (redraw) render_rows(drivers, ui, selected, status);
    }
    app->set_back_exits_app(true);
}
