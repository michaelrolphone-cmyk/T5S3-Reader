#include "T5AppApi.h"
#include "T5DriverManagerApi.h"
#include "T5PackageManagerApi.h"
#include "T5PackageVersion.h"
#include "T5UiApi.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LIMIT 64u
#define RECOVERY_LIMIT (LIMIT + 1u)
#define STATUS_BYTES 160u

typedef enum { ONLINE = 0, INBOX = 1 } source_t;
typedef enum { INSTALL = 0, UPDATE, CURRENT, NEWER, INVALID } action_t;
static source_t source;
static t5_driver_catalog_entry_t releases[LIMIT];
static uint32_t release_indices[LIMIT];
static t5_package_preview_t packages[LIMIT];
static char folders[LIMIT][T5_PACKAGE_ID_MAX];
static t5_ui_list_row_t rows[LIMIT];
static char names[LIMIT][96];
static char descriptions[LIMIT][128];
static char versions[LIMIT][40];
static uint32_t row_count;
static t5_driver_recovery_entry_t retained[RECOVERY_LIMIT];
static t5_ui_list_row_t retained_rows[RECOVERY_LIMIT + 1u];
static char retained_names[RECOVERY_LIMIT][96];
static char retained_descriptions[RECOVERY_LIMIT][128];
static char retained_versions[RECOVERY_LIMIT][40];

static bool driver_api(const t5_driver_manager_api_v1 *api) {
    return api && api->api_version == T5_DRIVER_MANAGER_API_VERSION &&
        api->struct_size >= offsetof(t5_driver_manager_api_v1, install) + sizeof(api->install) &&
        api->catalog_refresh && api->catalog_count && api->catalog_get &&
        api->installed_version_get && api->install;
}
static bool recovery_api(const t5_driver_manager_api_v1 *api) {
    return driver_api(api) &&
        api->struct_size >= offsetof(t5_driver_manager_api_v1, recovery_discard) + sizeof(api->recovery_discard) &&
        api->recovery_refresh && api->recovery_count && api->recovery_get &&
        api->recovery_retry && api->recovery_discard;
}
static bool package_api(const t5_package_manager_api_v1 *api) {
    return api && api->api_version == T5_PACKAGE_MANAGER_API_VERSION &&
        api->struct_size >= sizeof(t5_package_manager_api_v1) &&
        api->preview && api->install && api->uninstall;
}
static bool ui_api(const t5_ui_api_v1 *api) {
    return api && api->api_version == T5_UI_API_VERSION &&
        api->struct_size >= offsetof(t5_ui_api_v1, previous_index) + sizeof(api->previous_index) &&
        api->render_list && api->poll_event && api->hit_test && api->next_index && api->previous_index;
}
static int32_t menu(const t5_ui_api_v1 *ui, const char *title, const char *subtitle,
                    const t5_ui_list_row_t *choices, uint32_t count) {
    if (!count) return -1;
    const t5_ui_chrome_t chrome = {title, subtitle, "Confirm to select; Back cancels",
                                   "Cancel", "Select", "Up", "Down"};
    int32_t selected = 0;
    ui->render_list(&chrome, choices, count, selected);
    for (;;) {
        t5_ui_event_t event = {0};
        if (!ui->poll_event(&event, 20) || event.type == T5_UI_EVENT_BACK ||
            event.type == T5_UI_EVENT_EXIT) return -1;
        if (event.type == T5_UI_EVENT_PREVIOUS) selected = ui->previous_index(selected, count);
        if (event.type == T5_UI_EVENT_NEXT) selected = ui->next_index(selected, count);
        if (event.type == T5_UI_EVENT_TAP) {
            const int32_t hit = ui->hit_test(event.touch_x, event.touch_y);
            if (hit < 0 || hit >= (int32_t)count) continue;
            selected = hit;
        }
        if (event.type == T5_UI_EVENT_CONFIRM) return selected;
        ui->render_list(&chrome, choices, count, selected);
    }
}
static bool confirm(const t5_ui_api_v1 *ui, const char *id, const char *action) {
    const t5_ui_list_row_t options[2] = {
        {"Cancel", "Leave all packages untouched", "Back", 0},
        {action, "This operation cannot be undone", "Confirm", T5_UI_LIST_HIGHLIGHT_VALUE},
    };
    return menu(ui, "Confirm operation", id, options, 2) == 1;
}
static const char *recovery_state(uint8_t state) {
    switch (state) {
        case T5_DRIVER_RECOVERY_INCOMPLETE: return "Interrupted download";
        case T5_DRIVER_RECOVERY_READY: return "Verified stage; retry available";
        case T5_DRIVER_RECOVERY_INVALID: return "Invalid stage";
        case T5_DRIVER_RECOVERY_STALE: return "Stale stage";
        case T5_DRIVER_RECOVERY_MAPPED: return "Mapped driver; stop before recovery";
        default: return "Unresolved stage; manual repair";
    }
}
static bool recovery_screen(const t5_driver_manager_api_v1 *api, const t5_ui_api_v1 *ui) {
    if (!recovery_api(api) || !api->recovery_refresh()) return true;
    char status[STATUS_BYTES] = "Retained files are never deleted automatically";
    int32_t selected = 0;
    for (;;) {
        uint32_t count = api->recovery_count();
        if (count > RECOVERY_LIMIT) count = RECOVERY_LIMIT;
        for (uint32_t i = 0; i < count; ++i) {
            retained[i] = (t5_driver_recovery_entry_t){0};
            const bool valid = api->recovery_get(i, &retained[i]);
            const t5_driver_recovery_entry_t *entry = &retained[i];
            snprintf(retained_names[i], sizeof(retained_names[i]), "%s", valid ?
                (entry->kind == T5_DRIVER_RECOVERY_DOWNLOAD ? "Interrupted download" : entry->id) :
                "Inspection unavailable");
            snprintf(retained_descriptions[i], sizeof(retained_descriptions[i]), "%s",
                valid ? recovery_state(entry->state) : "Manual repair required");
            snprintf(retained_versions[i], sizeof(retained_versions[i]), "%s",
                valid ? entry->candidate_version : "");
            retained_rows[i] = (t5_ui_list_row_t){retained_names[i], retained_descriptions[i],
                retained_versions[i], entry->can_retry ? T5_UI_LIST_HIGHLIGHT_VALUE : 0};
        }
        retained_rows[count] = (t5_ui_list_row_t){"Continue to catalog", "Keep all remaining files", "Continue", 0};
        if (selected < 0 || selected > (int32_t)count) selected = (int32_t)count;
        const t5_ui_chrome_t chrome = {"Driver recovery", "Offline recovery", status,
                                       "Catalog", "Actions", "Up", "Down"};
        ui->render_list(&chrome, retained_rows, count + 1u, selected);
        t5_ui_event_t event = {0};
        if (!ui->poll_event(&event, 20)) return false;
        if (event.type == T5_UI_EVENT_EXIT) return false;
        if (event.type == T5_UI_EVENT_BACK) return true;
        if (event.type == T5_UI_EVENT_PREVIOUS) selected = ui->previous_index(selected, count + 1u);
        if (event.type == T5_UI_EVENT_NEXT) selected = ui->next_index(selected, count + 1u);
        if (event.type == T5_UI_EVENT_TAP) {
            const int32_t hit = ui->hit_test(event.touch_x, event.touch_y);
            if (hit == T5_UI_HIT_HEADER) {
                if (!api->recovery_refresh()) snprintf(status, sizeof(status), "Recovery refresh failed");
                continue;
            }
            if (hit < 0 || hit > (int32_t)count) continue;
            selected = hit;
        }
        if (event.type != T5_UI_EVENT_CONFIRM && event.type != T5_UI_EVENT_TAP) continue;
        if (selected == (int32_t)count) return true;
        const t5_driver_recovery_entry_t item = retained[selected];
        t5_ui_list_row_t actions[3] = {{"Cancel", "Keep retained files", "Back", 0}, {0}, {0}};
        uint32_t choices = 1;
        int32_t retry = -1, discard = -1;
        if (item.can_retry) {
            retry = (int32_t)choices;
            actions[choices++] = (t5_ui_list_row_t){"Retry verified stage", "Rehash and publish without download", "Retry", 0};
        }
        if (item.can_discard) {
            discard = (int32_t)choices;
            actions[choices++] = (t5_ui_list_row_t){"Discard stage", "Requires separate confirmation", "Discard", 0};
        }
        const int32_t action = menu(ui, "Recovery actions", item.id, actions, choices);
        if (retry >= 0 && action == retry) {
            const bool ok = api->recovery_retry((uint32_t)selected);
            snprintf(status, sizeof(status), "%s", ok ? "Verified stage published" : "Retry refused; files retained");
        } else if (discard >= 0 && action == discard &&
                   confirm(ui, item.id, "Discard retained files")) {
            const bool ok = api->recovery_discard((uint32_t)selected);
            snprintf(status, sizeof(status), "%s", ok ? "Retained stage discarded" : "Discard refused; files retained");
        }
        if (!api->recovery_refresh()) snprintf(status, sizeof(status), "Recovery refresh failed");
        selected = 0;
    }
}
static action_t action_for(const t5_driver_manager_api_v1 *api, uint32_t row,
                           char *installed, size_t capacity) {
    if (!installed || !capacity || row >= LIMIT) return INVALID;
    installed[0] = '\0';
    if (!api->installed_version_get(releases[row].id, installed, capacity)) return INSTALL;
    switch (t5_package_version_compare(releases[row].version, installed)) {
        case 1: return UPDATE;
        case 0: return CURRENT;
        case -1: return NEWER;
        default: return INVALID;
    }
}
static bool load_release(const t5_driver_manager_api_v1 *api, const t5_ui_api_v1 *ui) {
    const t5_ui_list_row_t wait = {"Loading release drivers", "Using saved Wi-Fi", "", 0};
    const t5_ui_chrome_t chrome = {"Driver Manager", "Release catalog", "Connecting...",
                                   "Back", "", "", ""};
    ui->render_list(&chrome, &wait, 1, 0);
    if (!api->catalog_refresh()) { row_count = 0; return false; }
    row_count = 0;
    uint32_t count = api->catalog_count();
    if (count > LIMIT) count = LIMIT;
    for (uint32_t index = 0; index < count; ++index) {
        t5_driver_catalog_entry_t entry = {0};
        if (!api->catalog_get(index, &entry) || !entry.id[0] || !entry.version[0]) continue;
        releases[row_count] = entry;
        release_indices[row_count] = index;
        char installed[T5_DRIVER_VERSION_MAX] = {0};
        const action_t action = action_for(api, row_count, installed, sizeof(installed));
        snprintf(names[row_count], sizeof(names[row_count]), "%s", entry.id);
        if (action == CURRENT)
            snprintf(descriptions[row_count], sizeof(descriptions[row_count]), "%s - Installed", entry.capability);
        else if (action == NEWER)
            snprintf(descriptions[row_count], sizeof(descriptions[row_count]), "Installed newer %s", installed);
        else if (action == INVALID)
            snprintf(descriptions[row_count], sizeof(descriptions[row_count]), "Installed version invalid");
        else if (action == UPDATE)
            snprintf(descriptions[row_count], sizeof(descriptions[row_count]), "%s - Installed %s", entry.capability, installed);
        else
            snprintf(descriptions[row_count], sizeof(descriptions[row_count]), "%s - Not installed", entry.capability);
        snprintf(versions[row_count], sizeof(versions[row_count]), "%s", entry.version);
        rows[row_count] = (t5_ui_list_row_t){names[row_count], descriptions[row_count], versions[row_count],
                         action == UPDATE ? T5_UI_LIST_HIGHLIGHT_VALUE : 0};
        ++row_count;
    }
    return true;
}
static bool load_inbox(const t5_app_api_v1 *app, const t5_package_manager_api_v1 *manager) {
    row_count = 0;
    if (!app->dir_open("/sd/Packages/Inbox")) return false;
    t5_app_dirent_t entry = {0};
    while (app->dir_next(&entry)) {
        if (!entry.is_directory || row_count >= LIMIT) continue;
        t5_package_preview_t info = {0};
        if (!manager->preview(entry.name, &info) || info.kind != T5_PACKAGE_DRIVER) continue;
        packages[row_count] = info;
        snprintf(folders[row_count], sizeof(folders[row_count]), "%s", entry.name);
        snprintf(names[row_count], sizeof(names[row_count]), "%s", info.id);
        if (!info.valid_installation)
            snprintf(descriptions[row_count], sizeof(descriptions[row_count]), "Installed generation requires recovery");
        else if (info.installed_version[0])
            snprintf(descriptions[row_count], sizeof(descriptions[row_count]), "Installed %s; %s",
                     info.installed_version, info.install_allowed ? "update available" : "no update");
        else
            snprintf(descriptions[row_count], sizeof(descriptions[row_count]), "%s",
                     info.install_allowed ? "SD inbox: ready" : "Dependency/version/stage blocked");
        snprintf(versions[row_count], sizeof(versions[row_count]), "%s", info.version);
        rows[row_count] = (t5_ui_list_row_t){names[row_count], descriptions[row_count], versions[row_count],
                         info.install_allowed ? T5_UI_LIST_HIGHLIGHT_VALUE : 0};
        ++row_count;
    }
    app->dir_close();
    return true;
}
static const char *action_label(const t5_driver_manager_api_v1 *api, int32_t selected) {
    if (selected < 0 || selected >= (int32_t)row_count) return "";
    if (source == INBOX) {
        const t5_package_preview_t *info = &packages[selected];
        if (!info->valid_installation) return "";
        if (info->install_allowed) return info->installed_version[0] ? "Update" : "Install";
        return info->installed_version[0] ? "Actions" : "";
    }
    char installed[T5_DRIVER_VERSION_MAX] = {0};
    const action_t action = action_for(api, (uint32_t)selected, installed, sizeof(installed));
    return action == UPDATE ? "Update" : action == INSTALL ? "Install" : "";
}
static void render(const t5_driver_manager_api_v1 *api, const t5_ui_api_v1 *ui,
                   int32_t selected, const char *status) {
    const t5_ui_chrome_t chrome = {"Driver Manager",
        source == INBOX ? "SD packages; tap header for options" : "Release drivers; tap header for options",
        status ? status : "", "Back", action_label(api, selected), "Up", "Down"};
    if (row_count) ui->render_list(&chrome, rows, row_count, selected);
    else {
        const t5_ui_list_row_t empty = {"No drivers available", source == INBOX ?
            "Add a driver to /Packages/Inbox/<id>" : "Latest release has no driver packages", "", 0};
        ui->render_list(&chrome, &empty, 1, 0);
    }
}
static void activate(const t5_driver_manager_api_v1 *api, const t5_package_manager_api_v1 *manager,
                     const t5_ui_api_v1 *ui, int32_t selected, char *status, size_t capacity) {
    if (selected < 0 || selected >= (int32_t)row_count) return;
    if (source == INBOX) {
        const t5_package_preview_t info = packages[selected];
        if (!info.valid_installation) snprintf(status, capacity, "%s: recovery required", info.id);
        else if (info.install_allowed) {
            const bool ok = manager->install(folders[selected]);
            snprintf(status, capacity, "%s: %s; not activated", info.id,
                ok ? "verified package installed" : "install refused; inspect stage/dependencies");
        } else if (info.installed_version[0] && confirm(ui, info.id, "Uninstall driver")) {
            const bool ok = manager->uninstall(T5_PACKAGE_DRIVER, info.id);
            snprintf(status, capacity, "%s: %s", info.id,
                ok ? "uninstalled" : "uninstall refused; stop mapped driver");
        } else snprintf(status, capacity, "%s: install blocked", info.id);
        return;
    }
    char installed[T5_DRIVER_VERSION_MAX] = {0};
    const action_t action = action_for(api, (uint32_t)selected, installed, sizeof(installed));
    if (action == CURRENT) { snprintf(status, capacity, "%s already current", releases[selected].id); return; }
    if (action == NEWER) { snprintf(status, capacity, "%s: installed %s is newer", releases[selected].id, installed); return; }
    if (action == INVALID) { snprintf(status, capacity, "%s: version invalid", releases[selected].id); return; }
    const bool ok = api->install(release_indices[selected]);
    snprintf(status, capacity, "%s: %s; not activated", releases[selected].id,
             ok ? "installed" : "install refused; inspect recovery");
}
static void header(const t5_app_api_v1 *app, const t5_driver_manager_api_v1 *api,
                   const t5_package_manager_api_v1 *manager, const t5_ui_api_v1 *ui,
                   char *status, size_t capacity) {
    const t5_ui_list_row_t options[4] = {
        {"Release catalog", "Refresh online drivers", "Online", 0},
        {"SD inbox", "Manage canonical driver packages", "SD", 0},
        {"Recovery", "Inspect/retry/discard retained stages", "Recovery", 0},
        {"Cancel", "Return to current view", "Back", 0},
    };
    const int32_t choice = menu(ui, "Driver Manager", "Select source or recovery", options, 4);
    if (choice == 0) {
        source = ONLINE;
        if (!load_release(api, ui)) {
            source = INBOX; (void)load_inbox(app, manager);
            snprintf(status, capacity, "Online unavailable; SD packages remain usable");
        } else status[0] = '\0';
    } else if (choice == 1) {
        source = INBOX; (void)load_inbox(app, manager); status[0] = '\0';
    } else if (choice == 2) {
        (void)recovery_screen(api, ui);
        if (source == INBOX) (void)load_inbox(app, manager);
        else if (!load_release(api, ui)) {
            source = INBOX; (void)load_inbox(app, manager);
            snprintf(status, capacity, "Online unavailable; SD packages remain usable");
        }
    }
}
__attribute__((visibility("default"))) void app_main(void) {
    const t5_app_api_v1 *app = t5_app_get_api(T5_APP_ABI_VERSION);
    const t5_driver_manager_api_v1 *drivers = t5_driver_manager_get_api(T5_DRIVER_MANAGER_API_VERSION);
    const t5_package_manager_api_v1 *manager = t5_package_manager_get_api(T5_PACKAGE_MANAGER_API_VERSION);
    const t5_ui_api_v1 *ui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!app || !app->dir_open || !app->dir_next || !app->dir_close ||
        !app->set_back_exits_app || !driver_api(drivers) || !package_api(manager) || !ui_api(ui)) return;
    app->set_back_exits_app(false);
    if (!recovery_screen(drivers, ui)) { app->set_back_exits_app(true); return; }
    source = ONLINE;
    char status[STATUS_BYTES] = {0};
    if (!load_release(drivers, ui)) {
        source = INBOX; (void)load_inbox(app, manager);
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
                if (source == INBOX) (void)load_inbox(app, manager);
                else if (!load_release(drivers, ui)) {
                    source = INBOX; (void)load_inbox(app, manager);
                    snprintf(status, sizeof(status), "Online unavailable; managing SD packages");
                }
                redraw = true; break;
            case T5_UI_EVENT_TAP: {
                const int32_t hit = ui->hit_test(event.touch_x, event.touch_y);
                if (hit == T5_UI_HIT_HEADER) {
                    header(app, drivers, manager, ui, status, sizeof(status));
                    selected = 0; redraw = true;
                } else if (hit >= 0 && hit < (int32_t)row_count) {
                    if (hit == selected) {
                        activate(drivers, manager, ui, selected, status, sizeof(status));
                        if (source == INBOX) (void)load_inbox(app, manager);
                        else if (!load_release(drivers, ui)) {
                            source = INBOX; (void)load_inbox(app, manager);
                            snprintf(status, sizeof(status), "Online unavailable; managing SD packages");
                        }
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
