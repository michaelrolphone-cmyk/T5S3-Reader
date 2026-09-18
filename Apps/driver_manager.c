#include "T5AppApi.h"
#include "T5DriverManagerApi.h"
#include "T5PackageManagerApi.h"
#include "T5PackageVersion.h"
#include "T5UiApi.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_ITEMS 64u
#define MAX_RECOVERY (MAX_ITEMS + 1u)
#define TITLE_MAX 96u
#define SUBTITLE_MAX 128u
#define VALUE_MAX 40u
#define STATUS_MAX 160u

typedef enum { VIEW_RELEASE = 0, VIEW_INBOX = 1 } driver_view_t;
typedef enum {
    ACTION_INSTALL, ACTION_UPDATE, ACTION_CURRENT, ACTION_INSTALLED_NEWER, ACTION_UNAVAILABLE
} driver_action_t;

static driver_view_t view;
static t5_driver_catalog_entry_t catalog[MAX_ITEMS];
static t5_package_preview_t inbox[MAX_ITEMS];
static char folders[MAX_ITEMS][T5_PACKAGE_ID_MAX];
static t5_ui_list_row_t rows[MAX_ITEMS];
static char titles[MAX_ITEMS][TITLE_MAX];
static char subtitles[MAX_ITEMS][SUBTITLE_MAX];
static char values[MAX_ITEMS][VALUE_MAX];
static uint32_t row_count;
static t5_driver_recovery_entry_t recovery[MAX_RECOVERY];
static t5_ui_list_row_t recovery_rows[MAX_RECOVERY + 1u];
static char recovery_titles[MAX_RECOVERY][TITLE_MAX];
static char recovery_subtitles[MAX_RECOVERY][SUBTITLE_MAX];
static char recovery_values[MAX_RECOVERY][VALUE_MAX];

static bool driver_ready(const t5_driver_manager_api_v1 *api) {
    const size_t required = offsetof(t5_driver_manager_api_v1, install) + sizeof(api->install);
    return api && api->api_version == T5_DRIVER_MANAGER_API_VERSION && api->struct_size >= required &&
           api->catalog_refresh && api->catalog_count && api->catalog_get &&
           api->installed_version_get && api->install;
}
static bool recovery_ready(const t5_driver_manager_api_v1 *api) {
    const size_t required = offsetof(t5_driver_manager_api_v1, recovery_discard) + sizeof(api->recovery_discard);
    return driver_ready(api) && api->struct_size >= required && api->recovery_refresh &&
           api->recovery_count && api->recovery_get && api->recovery_retry && api->recovery_discard;
}
static bool package_ready(const t5_package_manager_api_v1 *api) {
    return api && api->api_version == T5_PACKAGE_MANAGER_API_VERSION &&
           api->struct_size >= sizeof(t5_package_manager_api_v1) &&
           api->preview && api->install && api->uninstall;
}
static bool ui_ready(const t5_ui_api_v1 *ui) {
    const size_t required = offsetof(t5_ui_api_v1, previous_index) + sizeof(ui->previous_index);
    return ui && ui->struct_size >= required && ui->render_list && ui->hit_test &&
           ui->poll_event && ui->next_index && ui->previous_index;
}
static int32_t choose(const t5_ui_api_v1 *ui, const char *heading, const char *subtitle,
                      const t5_ui_list_row_t *choices, uint32_t count) {
    if (!count) return -1;
    const t5_ui_chrome_t chrome = {heading, subtitle, "Back cancels", "Cancel", "Select", "Up", "Down"};
    int32_t selected = 0;
    ui->render_list(&chrome, choices, count, selected);
    for (;;) {
        t5_ui_event_t event = {0};
        if (!ui->poll_event(&event, 20) || event.type == T5_UI_EVENT_EXIT ||
            event.type == T5_UI_EVENT_BACK) return -1;
        if (event.type == T5_UI_EVENT_PREVIOUS) selected = ui->previous_index(selected, count);
        if (event.type == T5_UI_EVENT_NEXT) selected = ui->next_index(selected, count);
        if (event.type == T5_UI_EVENT_TAP) {
            const int32_t hit = ui->hit_test(event.touch_x, event.touch_y);
            if (hit < 0 || hit >= (int32_t)count) continue;
            selected = hit;
        }
        // Selecting a row does not execute a mutation: Confirm is mandatory.
        if (event.type == T5_UI_EVENT_CONFIRM) return selected;
        ui->render_list(&chrome, choices, count, selected);
    }
}
static bool confirm(const t5_ui_api_v1 *ui, const char *id, const char *label) {
    const t5_ui_list_row_t options[2] = {
        {"Cancel", "Keep installed and staged files", "Back", 0},
        {label, "Irreversible or device-affecting action", "Confirm", T5_UI_LIST_HIGHLIGHT_VALUE},
    };
    return choose(ui, "Confirm operation", id, options, 2) == 1;
}
static const char *recovery_state(uint8_t state) {
    switch (state) {
        case T5_DRIVER_RECOVERY_INCOMPLETE: return "Interrupted download - no manifest";
        case T5_DRIVER_RECOVERY_READY: return "Verified stage - retry without download";
        case T5_DRIVER_RECOVERY_INVALID: return "Invalid stage - inspect or discard";
        case T5_DRIVER_RECOVERY_STALE: return "Old/equal stage - cannot install";
        case T5_DRIVER_RECOVERY_MAPPED: return "Driver mapped - stop it first";
        default: return "Unresolved generation - manual repair";
    }
}
static bool show_recovery(const t5_driver_manager_api_v1 *drivers, const t5_ui_api_v1 *ui) {
    if (!recovery_ready(drivers) || !drivers->recovery_refresh()) return true;
    char status[STATUS_MAX] = "Nothing is discarded automatically";
    int32_t selected = 0;
    for (;;) {
        uint32_t count = drivers->recovery_count();
        if (count > MAX_RECOVERY) count = MAX_RECOVERY;
        for (uint32_t i = 0; i < count; ++i) {
            recovery[i] = (t5_driver_recovery_entry_t){0};
            const bool valid = drivers->recovery_get(i, &recovery[i]);
            const t5_driver_recovery_entry_t *entry = &recovery[i];
            snprintf(recovery_titles[i], sizeof(recovery_titles[i]), "%s", valid ?
                     (entry->kind == T5_DRIVER_RECOVERY_DOWNLOAD ? "Interrupted download" : entry->id) :
                     "Inspection unavailable");
            snprintf(recovery_subtitles[i], sizeof(recovery_subtitles[i]), "%s",
                     valid ? recovery_state(entry->state) : "Manual repair required");
            snprintf(recovery_values[i], sizeof(recovery_values[i]), "%s",
                     valid ? entry->candidate_version : "");
            recovery_rows[i] = (t5_ui_list_row_t){recovery_titles[i], recovery_subtitles[i],
                    recovery_values[i], entry->can_retry ? T5_UI_LIST_HIGHLIGHT_VALUE : 0};
        }
        recovery_rows[count] = (t5_ui_list_row_t){"Continue", "Keep all remaining files", "Catalog", 0};
        if (selected < 0 || selected > (int32_t)count) selected = (int32_t)count;
        const t5_ui_chrome_t chrome = {"Driver recovery", "Offline stage inspection", status,
                                       "Catalog", "Actions", "Up", "Down"};
        ui->render_list(&chrome, recovery_rows, count + 1u, selected);
        t5_ui_event_t event = {0};
        if (!ui->poll_event(&event, 20)) return false;
        if (event.type == T5_UI_EVENT_EXIT) return false;
        if (event.type == T5_UI_EVENT_BACK) return true;
        if (event.type == T5_UI_EVENT_PREVIOUS) selected = ui->previous_index(selected, count + 1u);
        if (event.type == T5_UI_EVENT_NEXT) selected = ui->next_index(selected, count + 1u);
        if (event.type == T5_UI_EVENT_TAP) {
            const int32_t hit = ui->hit_test(event.touch_x, event.touch_y);
            if (hit == T5_UI_HIT_HEADER) {
                if (!drivers->recovery_refresh()) snprintf(status, sizeof(status), "Recovery refresh failed");
                continue;
            }
            if (hit < 0 || hit > (int32_t)count) continue;
            selected = hit;
        }
        if (event.type != T5_UI_EVENT_CONFIRM && event.type != T5_UI_EVENT_TAP) continue;
        if (selected == (int32_t)count) return true;
        const t5_driver_recovery_entry_t entry = recovery[selected];
        t5_ui_list_row_t actions[3] = {{"Cancel", "Keep retained files", "Back", 0}, {0}, {0}};
        uint32_t action_count = 1;
        int32_t retry = -1, discard = -1;
        if (entry.can_retry) {
            retry = (int32_t)action_count;
            actions[action_count++] = (t5_ui_list_row_t){"Retry stage", "Rehash and publish; no download", "Retry", 0};
        }
        if (entry.can_discard) {
            discard = (int32_t)action_count;
            actions[action_count++] = (t5_ui_list_row_t){"Discard retained files", "Requires another confirmation", "Discard", 0};
        }
        const int32_t action = choose(ui, "Recovery actions", entry.id, actions, action_count);
        if (action == retry) {
            const bool ok = drivers->recovery_retry((uint32_t)selected);
            snprintf(status, sizeof(status), "%s", ok ? "Verified stage published" : "Retry refused; stage retained");
        } else if (action == discard && confirm(ui, entry.id, "Discard retained stage")) {
            const bool ok = drivers->recovery_discard((uint32_t)selected);
            snprintf(status, sizeof(status), "%s", ok ? "Retained stage discarded" : "Discard refused; files retained");
        }
        if (!drivers->recovery_refresh()) snprintf(status, sizeof(status), "Recovery refresh failed");
        selected = 0;
    }
}
static driver_action_t action_for(const t5_driver_manager_api_v1 *api, uint32_t index,
                                  char *installed, size_t capacity) {
    if (installed && capacity) installed[0] = '\0';
    if (index >= row_count || !installed || !capacity) return ACTION_UNAVAILABLE;
    if (!api->installed_version_get(catalog[index].id, installed, capacity)) return ACTION_INSTALL;
    switch (t5_package_version_compare(catalog[index].version, installed)) {
        case 1: return ACTION_UPDATE;
        case 0: return ACTION_CURRENT;
        case -1: return ACTION_INSTALLED_NEWER;
        default: return ACTION_UNAVAILABLE;
    }
}
static bool refresh_release(const t5_driver_manager_api_v1 *api, const t5_ui_api_v1 *ui) {
    const t5_ui_list_row_t loading = {"Loading driver catalog", "Using saved Wi-Fi", "", 0};
    const t5_ui_chrome_t chrome = {"Driver Manager", "GitHub release drivers", "Connecting...",
                                   "Back", "", "", ""};
    ui->render_list(&chrome, &loading, 1, 0);
    if (!api->catalog_refresh()) return false;
    row_count = 0;
    uint32_t count = api->catalog_count();
    if (count > MAX_ITEMS) count = MAX_ITEMS;
    for (uint32_t i = 0; i < count; ++i) {
        t5_driver_catalog_entry_t entry = {0};
        if (!api->catalog_get(i, &entry) || !entry.id[0] || !entry.version[0]) continue;
        catalog[row_count] = entry;
        char installed[T5_DRIVER_VERSION_MAX] = {0};
        const driver_action_t action = action_for(api, row_count, installed, sizeof(installed));
        snprintf(titles[row_count], sizeof(titles[row_count]), "%s", entry.id);
        if (action == ACTION_CURRENT)
            snprintf(subtitles[row_count], sizeof(subtitles[row_count]), "%s - Installed", entry.capability);
        else if (action == ACTION_INSTALLED_NEWER)
            snprintf(subtitles[row_count], sizeof(subtitles[row_count]), "Installed newer %s", installed);
        else if (action == ACTION_UNAVAILABLE)
            snprintf(subtitles[row_count], sizeof(subtitles[row_count]), "Installed version invalid");
        else if (action == ACTION_UPDATE)
            snprintf(subtitles[row_count], sizeof(subtitles[row_count]), "%s - Installed %s", entry.capability, installed);
        else
            snprintf(subtitles[row_count], sizeof(subtitles[row_count]), "%s - Not installed", entry.capability);
        snprintf(values[row_count], sizeof(values[row_count]), "%s", entry.version);
        rows[row_count] = (t5_ui_list_row_t){titles[row_count], subtitles[row_count], values[row_count],
                         action == ACTION_UPDATE ? T5_UI_LIST_HIGHLIGHT_VALUE : 0};
        ++row_count;
    }
    return true;
}
static bool refresh_inbox(const t5_app_api_v1 *app, const t5_package_manager_api_v1 *manager) {
    row_count = 0;
    if (!app->dir_open("/sd/Packages/Inbox")) return false;
    t5_app_dirent_t entry = {0};
    while (app->dir_next(&entry)) {
        if (!entry.is_directory || row_count >= MAX_ITEMS) continue;
        t5_package_preview_t info = {0};
        if (!manager->preview(entry.name, &info) || info.kind != T5_PACKAGE_DRIVER) continue;
        inbox[row_count] = info;
        snprintf(folders[row_count], sizeof(folders[row_count]), "%s", entry.name);
        snprintf(titles[row_count], sizeof(titles[row_count]), "%s", info.id);
        if (!info.valid_installation)
            snprintf(subtitles[row_count], sizeof(subtitles[row_count]), "Installed generation requires recovery");
        else if (info.installed_version[0])
            snprintf(subtitles[row_count], sizeof(subtitles[row_count]), "Installed %s; %s",
                     info.installed_version, info.install_allowed ? "update available" : "no update");
        else
            snprintf(subtitles[row_count], sizeof(subtitles[row_count]), "%s",
                     info.install_allowed ? "SD inbox: ready to install" : "Dependency, version or stage blocked");
        snprintf(values[row_count], sizeof(values[row_count]), "%s", info.version);
        rows[row_count] = (t5_ui_list_row_t){titles[row_count], subtitles[row_count], values[row_count],
                         info.install_allowed ? T5_UI_LIST_HIGHLIGHT_VALUE : 0};
        ++row_count;
    }
    app->dir_close();
    return true;
}
static const char *confirm_label(const t5_driver_manager_api_v1 *drivers, int32_t selected) {
    if (selected < 0 || selected >= (int32_t)row_count) return "";
    if (view == VIEW_INBOX) {
        const t5_package_preview_t *info = &inbox[selected];
        if (!info->valid_installation) return "";
        if (info->install_allowed) return info->installed_version[0] ? "Update" : "Install";
        return info->installed_version[0] ? "Actions" : "";
    }
    char installed[T5_DRIVER_VERSION_MAX] = {0};
    const driver_action_t action = action_for(drivers, (uint32_t)selected, installed, sizeof(installed));
    return action == ACTION_UPDATE ? "Update" : action == ACTION_INSTALL ? "Install" : "";
}
static void render(const t5_driver_manager_api_v1 *drivers, const t5_ui_api_v1 *ui,
                   int32_t selected, const char *status) {
    const t5_ui_chrome_t chrome = {
        "Driver Manager", view == VIEW_INBOX ? "SD packages; tap header for options" :
        "GitHub drivers; tap header for options", status ? status : "", "Back",
        confirm_label(drivers, selected), "Up", "Down"};
    if (row_count) ui->render_list(&chrome, rows, row_count, selected);
    else {
        const t5_ui_list_row_t empty = {"No driver packages", view == VIEW_INBOX ?
            "Add driver to /Packages/Inbox/<id>" : "No drivers in latest release", "", 0};
        ui->render_list(&chrome, &empty, 1, 0);
    }
}
static void activate(const t5_driver_manager_api_v1 *drivers,
                     const t5_package_manager_api_v1 *manager,
                     const t5_ui_api_v1 *ui, int32_t selected,
                     char *status, size_t capacity) {
    if (selected < 0 || selected >= (int32_t)row_count) return;
    if (view == VIEW_INBOX) {
        const t5_package_preview_t info = inbox[selected];
        if (!info.valid_installation) {
            snprintf(status, capacity, "%s: recovery required", info.id);
        } else if (info.install_allowed) {
            const bool ok = manager->install(folders[selected]);
            snprintf(status, capacity, "%s: %s; no activation", info.id,
                     ok ? "verified package installed" : "install refused; check stage/dependencies");
        } else if (info.installed_version[0] && confirm(ui, info.id, "Uninstall driver")) {
            const bool ok = manager->uninstall(T5_PACKAGE_DRIVER, info.id);
            snprintf(status, capacity, "%s: %s", info.id,
                     ok ? "uninstalled" : "uninstall refused; stop mapped driver");
        } else if (!info.installed_version[0]) {
            snprintf(status, capacity, "%s: dependency/version/stage blocked", info.id);
        }
        return;
    }
    char installed[T5_DRIVER_VERSION_MAX] = {0};
    const driver_action_t action = action_for(drivers, (uint32_t)selected, installed, sizeof(installed));
    if (action == ACTION_CURRENT) {
        snprintf(status, capacity, "%s already current", catalog[selected].id); return;
    }
    if (action == ACTION_INSTALLED_NEWER) {
        snprintf(status, capacity, "%s: installed %s is newer", catalog[selected].id, installed); return;
    }
    if (action == ACTION_UNAVAILABLE) {
        snprintf(status, capacity, "%s: installed version invalid", catalog[selected].id); return;
    }
    const bool ok = drivers->install((uint32_t)selected);
    snprintf(status, capacity, "%s: %s; no activation", catalog[selected].id,
             ok ? "package installed" : "install refused; inspect recovery");
}
static void choose_header(const t5_app_api_v1 *app,
                          const t5_driver_manager_api_v1 *drivers,
                          const t5_package_manager_api_v1 *manager,
                          const t5_ui_api_v1 *ui,
                          char *status, size_t capacity) {
    const t5_ui_list_row_t options[4] = {
        {"Release catalog", "Refresh online drivers", "Online", 0},
        {"SD inbox", "Manage canonical driver packages", "SD", 0},
        {"Recover retained files", "Inspect/retry/explicit discard offline", "Recovery", 0},
        {"Cancel", "Return to current view", "Back", 0},
    };
    const int32_t action = choose(ui, "Driver Manager", "Select catalog or recovery", options, 4);
    if (action == 0) {
        view = VIEW_RELEASE;
        if (!refresh_release(drivers, ui)) {
            snprintf(status, capacity, "Online refresh failed; SD inbox remains available");
            view = VIEW_INBOX;
            (void)refresh_inbox(app, manager);
        } else status[0] = '\0';
    } else if (action == 1) {
        view = VIEW_INBOX;
        (void)refresh_inbox(app, manager);
        status[0] = '\0';
    } else if (action == 2) {
        (void)show_recovery(drivers, ui);
        if (view == VIEW_INBOX) (void)refresh_inbox(app, manager);
        else if (!refresh_release(drivers, ui)) snprintf(status, capacity, "Release refresh failed");
    }
}

__attribute__((visibility("default"))) void app_main(void) {
    const t5_app_api_v1 *app = t5_app_get_api(T5_APP_ABI_VERSION);
    const t5_driver_manager_api_v1 *drivers = t5_driver_manager_get_api(T5_DRIVER_MANAGER_API_VERSION);
    const t5_package_manager_api_v1 *manager = t5_package_manager_get_api(T5_PACKAGE_MANAGER_API_VERSION);
    const t5_ui_api_v1 *ui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!app || !app->dir_open || !app->dir_next || !app->dir_close ||
        !app->set_back_exits_app || !driver_ready(drivers) || !package_ready(manager) || !ui_ready(ui)) return;
    app->set_back_exits_app(false);
    if (recovery_ready(drivers) && !show_recovery(drivers, ui)) {
        app->set_back_exits_app(true); return;
    }
    view = VIEW_RELEASE;
    char status[STATUS_MAX] = {0};
    if (!refresh_release(drivers, ui)) {
        view = VIEW_INBOX;
        (void)refresh_inbox(app, manager);
        snprintf(status, sizeof(status), "Online unavailable; managing SD packages");
    }
    int32_t selected = 0;
    render(drivers, ui, selected, status);
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
                activate(drivers, manager, ui, selected, status, sizeof(status));
                if (view == VIEW_INBOX) (void)refresh_inbox(app, manager);
                else if (!refresh_release(drivers, ui)) snprintf(status, sizeof(status), "Release refresh failed");
                redraw = true; break;
            case T5_UI_EVENT_TAP: {
                const int32_t hit = ui->hit_test(event.touch_x, event.touch_y);
                if (hit == T5_UI_HIT_HEADER) {
                    choose_header(app, drivers, manager, ui, status, sizeof(status));
                    selected = 0; redraw = true;
                } else if (hit >= 0 && hit < (int32_t)row_count) {
                    if (hit == selected) {
                        activate(drivers, manager, ui, selected, status, sizeof(status));
                        if (view == VIEW_INBOX) (void)refresh_inbox(app, manager);
                        else if (!refresh_release(drivers, ui)) snprintf(status, sizeof(status), "Release refresh failed");
                    } else { selected = hit; status[0] = '\0'; }
                    redraw = true;
                }
                break;
            }
            case T5_UI_EVENT_BACK:
            case T5_UI_EVENT_EXIT:
                app->set_back_exits_app(true); return;
            default: break;
        }
        if (!row_count || selected >= (int32_t)row_count) selected = 0;
        if (redraw) render(drivers, ui, selected, status);
    }
    app->set_back_exits_app(true);
}
