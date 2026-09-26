#include "T5AppApi.h"
#include "T5DriverManagerApi.h"
#include "T5PackageManagerApi.h"
#include "T5UiApi.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Normal installations use the same source-neutral ordinary package API as
 * App Store and Package Manager. The legacy driver ABI is recovery-only. */
#define LIMIT 64u
#define RECOVERY_LIMIT (LIMIT + 1u)
#define STATUS_BYTES 160u
#define NAME_BYTES 160u

typedef enum { ONLINE, INBOX } source_t;
static source_t source;
static t5_package_catalog_row_t online[LIMIT];
static uint32_t online_index[LIMIT];
static t5_package_preview_t offline[LIMIT];
static char offline_name[LIMIT][NAME_BYTES];
static bool offline_zip[LIMIT];
static t5_ui_list_row_t rows[LIMIT];
static char names[LIMIT][96], descriptions[LIMIT][128], versions[LIMIT][40];
static uint32_t row_count;
static t5_driver_recovery_entry_t retained[RECOVERY_LIMIT];
static t5_ui_list_row_t retained_rows[RECOVERY_LIMIT + 1u];
static char retained_names[RECOVERY_LIMIT][96];
static char retained_details[RECOVERY_LIMIT][128];
static char retained_versions[RECOVERY_LIMIT][40];

static bool app_ready(const t5_app_api_v1 *app) {
    return app && app->dir_open && app->dir_next && app->dir_close &&
           app->set_back_exits_app;
}
static bool manager_ready(const t5_package_manager_api_v1 *api) {
    return api && api->api_version == T5_PACKAGE_MANAGER_API_VERSION &&
        api->struct_size >= offsetof(t5_package_manager_api_v1, online_install) +
                            sizeof(api->online_install) &&
        api->preview && api->install && api->uninstall &&
        api->preview_archive && api->install_archive &&
        api->online_refresh && api->online_count && api->online_get && api->online_install;
}
static bool recovery_ready(const t5_driver_manager_api_v1 *api) {
    return api && api->api_version == T5_DRIVER_MANAGER_API_VERSION &&
        api->struct_size >= offsetof(t5_driver_manager_api_v1, recovery_discard) +
                            sizeof(api->recovery_discard) &&
        api->recovery_refresh && api->recovery_count && api->recovery_get &&
        api->recovery_retry && api->recovery_discard;
}
static bool ui_ready(const t5_ui_api_v1 *ui) {
    return ui && ui->api_version == T5_UI_API_VERSION &&
        ui->struct_size >= offsetof(t5_ui_api_v1, previous_index) +
                           sizeof(ui->previous_index) &&
        ui->render_list && ui->poll_event && ui->hit_test &&
        ui->next_index && ui->previous_index;
}
static size_t length_limit(const char *s, size_t bound) {
    size_t n = 0;
    if (s) while (n < bound && s[n]) ++n;
    return n;
}
static bool zip_name(const char *s) {
    const size_t n = length_limit(s, NAME_BYTES);
    return n > 8 && n < NAME_BYTES && !strcmp(s + n - 8, ".rte.zip");
}
static void set_row(uint32_t i, const t5_package_preview_t *p, const char *detail) {
    snprintf(names[i], sizeof(names[i]), "%s", p->id);
    snprintf(descriptions[i], sizeof(descriptions[i]), "%s", detail);
    snprintf(versions[i], sizeof(versions[i]), "%s", p->version);
    rows[i] = (t5_ui_list_row_t){names[i], descriptions[i], versions[i],
                                p->install_allowed ? T5_UI_LIST_HIGHLIGHT_VALUE : 0};
}
static const char *package_state(const t5_package_preview_t *p, bool archived) {
    if (!p->valid_installation) return "Installed generation requires recovery";
    if (p->install_allowed) return p->installed_version[0] ? "Update available" :
        archived ? "SD ZIP: ready" : "Not installed";
    return p->installed_version[0] ? "Installed / no update" :
        "Dependencies, version or stage blocked";
}
static void populate_online(const t5_package_manager_api_v1 *manager) {
    row_count = 0;
    uint32_t count = manager->online_count();
    if (count > LIMIT) count = LIMIT;
    for (uint32_t i = 0; i < count; ++i) {
        t5_package_catalog_row_t item = {0};
        if (!manager->online_get(i, &item) ||
            item.package.kind != T5_PACKAGE_DRIVER || !item.package.id[0]) continue;
        online[row_count] = item;
        online_index[row_count] = i;
        set_row(row_count, &item.package, package_state(&item.package, false));
        ++row_count;
    }
}
static bool refresh_online(const t5_package_manager_api_v1 *manager,
                           const t5_ui_api_v1 *ui) {
    const t5_ui_list_row_t loading = {"Loading package catalog", "Using saved Wi-Fi", "", 0};
    const t5_ui_chrome_t chrome = {"Driver Manager", "Pinned package catalog", "Connecting...",
                                   "Back", "", "", ""};
    ui->render_list(&chrome, &loading, 1, 0);
    row_count = 0;
    if (!manager->online_refresh()) return false;
    populate_online(manager);
    return true;
}
static bool load_inbox(const t5_app_api_v1 *app,
                       const t5_package_manager_api_v1 *manager) {
    row_count = 0;
    if (!app->dir_open("/sd/Packages/Inbox")) return false;
    t5_app_dirent_t entry = {0};
    while (row_count < LIMIT && app->dir_next(&entry)) {
        const size_t n = length_limit(entry.name, sizeof(entry.name));
        if (!n || n >= sizeof(entry.name) || n >= NAME_BYTES ||
            (!entry.is_directory && !zip_name(entry.name))) continue;
        const bool archived = !entry.is_directory;
        t5_package_preview_t p = {0};
        const bool valid = archived ? manager->preview_archive(entry.name, &p) :
                                      manager->preview(entry.name, &p);
        if (!valid || p.kind != T5_PACKAGE_DRIVER) continue;
        memcpy(offline_name[row_count], entry.name, n + 1u);
        offline_zip[row_count] = archived;
        offline[row_count] = p;
        set_row(row_count, &p, package_state(&p, archived));
        ++row_count;
    }
    app->dir_close();
    return true;
}
static int32_t menu(const t5_ui_api_v1 *ui, const char *title, const char *subtitle,
                    const t5_ui_list_row_t *choices, uint32_t count) {
    const t5_ui_chrome_t chrome = {title, subtitle, "Confirm selects; Back cancels",
                                   "Cancel", "Select", "Up", "Down"};
    if (!count) return -1;
    int32_t selected = 0;
    ui->render_list(&chrome, choices, count, selected);
    for (;;) {
        t5_ui_event_t e = {0};
        if (!ui->poll_event(&e, 20) || e.type == T5_UI_EVENT_BACK ||
            e.type == T5_UI_EVENT_EXIT) return -1;
        if (e.type == T5_UI_EVENT_PREVIOUS) selected = ui->previous_index(selected, count);
        else if (e.type == T5_UI_EVENT_NEXT) selected = ui->next_index(selected, count);
        else if (e.type == T5_UI_EVENT_TAP) {
            const int32_t hit = ui->hit_test(e.touch_x, e.touch_y);
            if (hit >= 0 && hit < (int32_t)count) selected = hit;
        } else if (e.type == T5_UI_EVENT_CONFIRM) return selected;
        ui->render_list(&chrome, choices, count, selected);
    }
}
static bool confirm(const t5_ui_api_v1 *ui, const char *id, const char *operation) {
    const t5_ui_list_row_t options[] = {
        {"Cancel", "Leave all files intact", "Keep", 0},
        {operation, "Explicit approval required", "Confirm", T5_UI_LIST_HIGHLIGHT_VALUE},
    };
    return menu(ui, "Confirm operation", id, options, 2) == 1;
}
static const char *recovery_state(uint8_t s) {
    switch (s) {
        case T5_DRIVER_RECOVERY_INCOMPLETE: return "Interrupted download";
        case T5_DRIVER_RECOVERY_READY: return "Verified stage; retry available";
        case T5_DRIVER_RECOVERY_INVALID: return "Invalid stage";
        case T5_DRIVER_RECOVERY_STALE: return "Stale stage";
        case T5_DRIVER_RECOVERY_MAPPED: return "Mapped driver; stop first";
        default: return "Manual repair required";
    }
}
static bool recovery_screen(const t5_driver_manager_api_v1 *driver,
                            const t5_ui_api_v1 *ui) {
    if (!recovery_ready(driver) || !driver->recovery_refresh()) return true;
    int32_t selected = 0;
    char status[STATUS_BYTES] = "Retained files are not deleted automatically";
    for (;;) {
        uint32_t count = driver->recovery_count();
        if (!count) return true;
        if (count > RECOVERY_LIMIT) count = RECOVERY_LIMIT;
        for (uint32_t i = 0; i < count; ++i) {
            retained[i] = (t5_driver_recovery_entry_t){0};
            const bool valid = driver->recovery_get(i, &retained[i]);
            const t5_driver_recovery_entry_t *item = &retained[i];
            snprintf(retained_names[i], sizeof(retained_names[i]), "%s",
                !valid ? "Inspection unavailable" :
                item->kind == T5_DRIVER_RECOVERY_DOWNLOAD ? "Interrupted download" : item->id);
            snprintf(retained_details[i], sizeof(retained_details[i]), "%s",
                valid ? recovery_state(item->state) : "Manual inspection required");
            snprintf(retained_versions[i], sizeof(retained_versions[i]), "%s",
                valid ? item->candidate_version : "");
            retained_rows[i] = (t5_ui_list_row_t){retained_names[i], retained_details[i],
                retained_versions[i], valid && item->can_retry ? T5_UI_LIST_HIGHLIGHT_VALUE : 0};
        }
        retained_rows[count] = (t5_ui_list_row_t){"Continue to catalog", "Keep remaining files", "Continue", 0};
        if (selected < 0 || selected > (int32_t)count) selected = (int32_t)count;
        const t5_ui_chrome_t chrome = {"Driver recovery", "Retained stages", status,
                                       "Continue", "Actions", "Up", "Down"};
        ui->render_list(&chrome, retained_rows, count + 1u, selected);
        t5_ui_event_t e = {0};
        if (!ui->poll_event(&e, 20)) return false;
        if (e.type == T5_UI_EVENT_EXIT) return false;
        if (e.type == T5_UI_EVENT_BACK) return true;
        if (e.type == T5_UI_EVENT_PREVIOUS) selected = ui->previous_index(selected, count + 1u);
        if (e.type == T5_UI_EVENT_NEXT) selected = ui->next_index(selected, count + 1u);
        if (e.type == T5_UI_EVENT_TAP) {
            const int32_t hit = ui->hit_test(e.touch_x, e.touch_y);
            if (hit == T5_UI_HIT_HEADER) {
                if (!driver->recovery_refresh()) snprintf(status, sizeof(status), "Recovery refresh failed");
                continue;
            }
            if (hit < 0 || hit > (int32_t)count) continue;
            selected = hit;
        }
        if (e.type != T5_UI_EVENT_CONFIRM && e.type != T5_UI_EVENT_TAP) continue;
        if (selected == (int32_t)count) return true;
        const t5_driver_recovery_entry_t item = retained[selected];
        t5_ui_list_row_t options[3] = {{"Cancel", "Keep retained files", "Back", 0}, {0}, {0}};
        uint32_t n = 1;
        int32_t retry = -1, discard = -1;
        if (item.can_retry) {
            retry = (int32_t)n;
            options[n++] = (t5_ui_list_row_t){"Retry verified stage", "Rehash/publish offline", "Retry", 0};
        }
        if (item.can_discard) {
            discard = (int32_t)n;
            options[n++] = (t5_ui_list_row_t){"Discard stage", "Separate confirmation required", "Discard", 0};
        }
        const int32_t choice = menu(ui, "Recovery actions", item.id, options, n);
        if (retry >= 0 && choice == retry) {
            const bool ok = driver->recovery_retry((uint32_t)selected);
            snprintf(status, sizeof(status), "%s", ok ? "Verified stage published" : "Retry refused; files retained");
        } else if (discard >= 0 && choice == discard &&
                   confirm(ui, item.id, "Discard retained files")) {
            const bool ok = driver->recovery_discard((uint32_t)selected);
            snprintf(status, sizeof(status), "%s", ok ? "Retained stage discarded" : "Discard refused; files retained");
        }
        if (!driver->recovery_refresh()) snprintf(status, sizeof(status), "Recovery refresh failed");
        selected = 0;
    }
}
static const t5_package_preview_t *selected_package(int32_t index) {
    if (index < 0 || index >= (int32_t)row_count) return NULL;
    return source == ONLINE ? &online[index].package : &offline[index];
}
static void render(const t5_ui_api_v1 *ui, int32_t index, const char *status) {
    const t5_package_preview_t *p = selected_package(index);
    const char *action = p && p->valid_installation && p->install_allowed ?
        p->installed_version[0] ? "Update" : "Install" :
        p && source == INBOX && p->installed_version[0] ? "Actions" : "";
    const t5_ui_chrome_t chrome = {"Driver Manager", source == ONLINE ?
        "Generic release catalog; tap header for SD/recovery" :
        "SD ZIP packages; tap header for online/recovery",
        status ? status : "", "Back", action, "Up", "Down"};
    if (row_count) ui->render_list(&chrome, rows, row_count, index);
    else {
        const t5_ui_list_row_t empty = {"No drivers", source == ONLINE ?
            "No driver ZIP in catalog" : "Add .rte.zip to /Packages/Inbox", "", 0};
        ui->render_list(&chrome, &empty, 1, 0);
    }
}
static void activate(const t5_package_manager_api_v1 *manager, const t5_ui_api_v1 *ui,
                     int32_t selected, char *status, size_t capacity) {
    const t5_package_preview_t *p = selected_package(selected);
    if (!p) return;
    if (!p->valid_installation) {
        snprintf(status, capacity, "%.63s: recovery required", p->id);
    } else if (p->install_allowed) {
        const t5_ui_chrome_t busy = {"Driver installation", p->id,
            "Verifying and publishing; keep power on", "", "", "", ""};
        const t5_ui_list_row_t waiting = {"Installing", source == ONLINE ?
            "Pinned release ZIP + SHA-256 + transaction" :
            "Offline source + SHA-256 + transaction", "", 0};
        ui->render_list(&busy, &waiting, 1, 0);
        const bool ok = source == ONLINE ? manager->online_install(online_index[selected]) :
            offline_zip[selected] ? manager->install_archive(offline_name[selected]) :
                                    manager->install(offline_name[selected]);
        snprintf(status, capacity, "%.63s: %s; not activated", p->id,
                 ok ? "verified package installed" : "install refused; inspect dependency/stage");
    } else if (source == INBOX && p->installed_version[0] &&
               confirm(ui, p->id, "Uninstall driver")) {
        const bool ok = manager->uninstall(T5_PACKAGE_DRIVER, p->id);
        snprintf(status, capacity, "%.63s: %s", p->id,
                 ok ? "uninstalled" : "uninstall refused; stop mapped driver");
    } else if (!p->installed_version[0]) {
        snprintf(status, capacity, "%.63s: dependency/version/stage blocked", p->id);
    }
}
static void header(const t5_app_api_v1 *app, const t5_driver_manager_api_v1 *driver,
                   const t5_package_manager_api_v1 *manager, const t5_ui_api_v1 *ui,
                   char *status, size_t capacity) {
    const t5_ui_list_row_t choices[] = {
        {"Release catalog", "Generic driver ZIP packages", "Online", 0},
        {"SD inbox", "ZIPs and legacy directory sources", "SD", 0},
        {"Recovery", "Inspect/retry/discard retained stages", "Recovery", 0},
        {"Cancel", "Return to catalog", "Back", 0},
    };
    const int32_t choice = menu(ui, "Driver Manager", "Select source or recovery", choices, 4);
    if (choice == 0) {
        source = ONLINE;
        if (!refresh_online(manager, ui)) {
            source = INBOX; (void)load_inbox(app, manager);
            snprintf(status, capacity, "Online unavailable; SD packages remain usable");
        } else status[0] = 0;
    } else if (choice == 1) {
        source = INBOX; (void)load_inbox(app, manager); status[0] = 0;
    } else if (choice == 2) {
        (void)recovery_screen(driver, ui);
        if (source == INBOX) (void)load_inbox(app, manager);
        else populate_online(manager);
    }
}
__attribute__((visibility("default"))) void app_main(void) {
    const t5_app_api_v1 *app = t5_app_get_api(T5_APP_ABI_VERSION);
    const t5_driver_manager_api_v1 *driver = t5_driver_manager_get_api(T5_DRIVER_MANAGER_API_VERSION);
    const t5_package_manager_api_v1 *manager = t5_package_manager_get_api(T5_PACKAGE_MANAGER_API_VERSION);
    const t5_ui_api_v1 *ui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!app_ready(app) || !manager_ready(manager) || !ui_ready(ui)) return;
    app->set_back_exits_app(false);
    if (!recovery_screen(driver, ui)) { app->set_back_exits_app(true); return; }
    source = ONLINE;
    char status[STATUS_BYTES] = {0};
    if (!refresh_online(manager, ui)) {
        source = INBOX; (void)load_inbox(app, manager);
        snprintf(status, sizeof(status), "Online unavailable; SD packages remain usable");
    }
    int32_t selected = 0;
    render(ui, selected, status);
    for (;;) {
        t5_ui_event_t e = {0};
        if (!ui->poll_event(&e, 20)) break;
        bool redraw = false;
        switch (e.type) {
            case T5_UI_EVENT_PREVIOUS:
                selected = ui->previous_index(selected, row_count);
                status[0] = 0; redraw = true; break;
            case T5_UI_EVENT_NEXT:
                selected = ui->next_index(selected, row_count);
                status[0] = 0; redraw = true; break;
            case T5_UI_EVENT_CONFIRM:
                activate(manager, ui, selected, status, sizeof(status));
                if (source == INBOX) (void)load_inbox(app, manager);
                else populate_online(manager);
                redraw = true; break;
            case T5_UI_EVENT_TAP: {
                const int32_t hit = ui->hit_test(e.touch_x, e.touch_y);
                if (hit == T5_UI_HIT_HEADER) {
                    header(app, driver, manager, ui, status, sizeof(status));
                    selected = 0; redraw = true;
                } else if (hit >= 0 && hit < (int32_t)row_count) {
                    if (selected == hit) {
                        activate(manager, ui, selected, status, sizeof(status));
                        if (source == INBOX) (void)load_inbox(app, manager);
                        else populate_online(manager);
                    } else { selected = hit; status[0] = 0; }
                    redraw = true;
                }
                break;
            }
            case T5_UI_EVENT_BACK:
            case T5_UI_EVENT_EXIT:
                app->set_back_exits_app(true);
                return;
            default: break;
        }
        if (!row_count || selected < 0 || selected >= (int32_t)row_count) selected = 0;
        if (redraw) render(ui, selected, status);
    }
    app->set_back_exits_app(true);
}
