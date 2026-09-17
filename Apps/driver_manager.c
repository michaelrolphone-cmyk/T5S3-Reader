#include "T5AppApi.h"
#include "T5DriverManagerApi.h"
#include "T5DriverOfflineApi.h"
#include "T5UiApi.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_ITEMS 64u
#define ROW_CAP (MAX_ITEMS + 2u)
#define STATUS_CAP 160u

typedef enum { VIEW_LOCAL, VIEW_ONLINE } view_t;
static t5_ui_list_row_t rows[ROW_CAP];
static t5_driver_local_entry_t local_items[MAX_ITEMS];
static t5_driver_catalog_entry_t remote_items[MAX_ITEMS];
static char titles[ROW_CAP][80];
static char subtitles[ROW_CAP][112];
static char values[ROW_CAP][40];
static uint32_t row_count = 2;
static view_t view = VIEW_LOCAL;
static bool online_loaded = false;
static char status[STATUS_CAP];

static bool has_ui(const t5_ui_api_v1 *ui) {
    const size_t size = offsetof(t5_ui_api_v1, previous_index) + sizeof(ui->previous_index);
    return ui && ui->struct_size >= size && ui->render_list && ui->hit_test &&
           ui->poll_event && ui->next_index && ui->previous_index;
}
static bool has_offline(const t5_driver_offline_api_v1 *api) {
    const size_t size = offsetof(t5_driver_offline_api_v1, install) + sizeof(api->install);
    return api && api->api_version == T5_DRIVER_OFFLINE_API_VERSION && api->struct_size >= size &&
           api->refresh && api->count && api->get && api->install;
}
static bool has_online(const t5_driver_manager_api_v1 *api) {
    const size_t size = offsetof(t5_driver_manager_api_v1, install) + sizeof(api->install);
    return api && api->api_version == T5_DRIVER_MANAGER_API_VERSION && api->struct_size >= size &&
           api->catalog_refresh && api->catalog_count && api->catalog_get &&
           api->installed_version_get && api->install;
}
static void row(uint32_t index, const char *title, const char *subtitle, const char *value, uint8_t flags) {
    snprintf(titles[index], sizeof(titles[index]), "%s", title);
    snprintf(subtitles[index], sizeof(subtitles[index]), "%s", subtitle);
    snprintf(values[index], sizeof(values[index]), "%s", value);
    rows[index].title = titles[index];
    rows[index].subtitle = subtitles[index];
    rows[index].value = values[index];
    rows[index].flags = flags;
}
static void build_local(const t5_driver_offline_api_v1 *api) {
    row_count = 2;
    row(0, "Online release catalog", "Optional: connect to GitHub", "Online", 0);
    row(1, "Refresh SD inventory", "Installed drivers and /Drivers/Inbox", "Refresh", 0);
    uint32_t count = api->count();
    if (count > MAX_ITEMS) count = MAX_ITEMS;
    for (uint32_t i = 0; i < count; ++i) {
        t5_driver_local_entry_t item = {0};
        if (!api->get(i, &item)) continue;
        local_items[row_count - 2] = item;
        const bool valid = (item.flags & T5_DRIVER_LOCAL_VALID) != 0;
        const bool inbox = item.source == T5_DRIVER_LOCAL_INBOX;
        char info[112];
        snprintf(info, sizeof(info), "%s - %s", valid ? (item.capability[0] ? item.capability : "Driver") :
                 "Invalid manifest or ELF integrity", inbox ? "SD inbox" : "Installed");
        row(row_count, item.id, info, valid ? (item.version[0] ? item.version : "") : "INVALID",
            inbox && valid ? T5_UI_LIST_HIGHLIGHT_VALUE : 0);
        ++row_count;
    }
}
static void build_online(const t5_driver_manager_api_v1 *api) {
    row_count = 2;
    row(0, "Local SD drivers", "Installed packages and offline installation", "Local", 0);
    row(1, "Refresh release catalog", "Requires working Wi-Fi", "Refresh", 0);
    if (!online_loaded) return;
    uint32_t count = api->catalog_count();
    if (count > MAX_ITEMS) count = MAX_ITEMS;
    for (uint32_t i = 0; i < count; ++i) {
        t5_driver_catalog_entry_t item = {0};
        if (!api->catalog_get(i, &item) || !item.id[0] || !item.version[0]) continue;
        remote_items[row_count - 2] = item;
        char installed[T5_DRIVER_VERSION_MAX] = {0};
        const bool present = api->installed_version_get(item.id, installed, sizeof(installed));
        const bool current = present && strcmp(installed, item.version) == 0;
        char subtitle[112];
        snprintf(subtitle, sizeof(subtitle), "%s - %s", item.capability[0] ? item.capability : "Driver",
                 current ? "Installed" : present ? "Update available" : "Not installed");
        row(row_count, item.id, subtitle, item.version, current ? 0 : T5_UI_LIST_HIGHLIGHT_VALUE);
        ++row_count;
    }
}
static const char *confirm_label(const t5_driver_offline_api_v1 *local,
                                  const t5_driver_manager_api_v1 *remote, int32_t selected) {
    if (selected < 0 || selected >= (int32_t)row_count) return "";
    if (selected == 0) return "Open";
    if (selected == 1) return "Refresh";
    const uint32_t i = (uint32_t)selected - 2;
    if (view == VIEW_LOCAL) {
        (void)local;
        return local_items[i].source == T5_DRIVER_LOCAL_INBOX &&
               (local_items[i].flags & T5_DRIVER_LOCAL_VALID) ? "Install" : "";
    }
    if (!has_online(remote)) return "";
    char version[T5_DRIVER_VERSION_MAX] = {0};
    const bool installed = remote->installed_version_get(remote_items[i].id, version, sizeof(version));
    if (installed && strcmp(version, remote_items[i].version) == 0) return "";
    return installed ? "Update" : "Install";
}
static void render(const t5_ui_api_v1 *ui, const t5_driver_offline_api_v1 *local,
                   const t5_driver_manager_api_v1 *remote, int32_t selected) {
    const t5_ui_chrome_t chrome = {
        .title = "Driver Manager",
        .subtitle = view == VIEW_LOCAL ? "SD / offline" : "GitHub releases",
        .status = status,
        .back_label = "Back",
        .confirm_label = confirm_label(local, remote, selected),
        .previous_label = "Up",
        .next_label = "Down",
    };
    ui->render_list(&chrome, rows, row_count, selected);
}
static void refresh_local(const t5_driver_offline_api_v1 *local) {
    const bool ok = local->refresh();
    build_local(local);
    snprintf(status, sizeof(status), "%s", ok ?
             (row_count > 2 ? "SD inventory ready; offline mode" : "No installed or inbox drivers") :
             "SD inventory unavailable");
}
static void refresh_online(const t5_driver_manager_api_v1 *remote,
                           const t5_ui_api_v1 *ui, const t5_driver_offline_api_v1 *local) {
    if (!has_online(remote)) {
        online_loaded = false;
        build_online(remote);
        snprintf(status, sizeof(status), "Online catalog API unavailable; local mode works");
        return;
    }
    row_count = 2;
    row(0, "Local SD drivers", "Offline mode", "Local", 0);
    row(1, "Loading GitHub drivers", "Saved Wi-Fi", "", 0);
    snprintf(status, sizeof(status), "Connecting to release catalog...");
    render(ui, local, remote, 0);
    online_loaded = remote->catalog_refresh();
    build_online(remote);
    snprintf(status, sizeof(status), "%s", online_loaded ?
             (row_count > 2 ? "Release catalog loaded" : "No drivers in latest release") :
             "Release unavailable; select Local to manage SD drivers");
}
static void activate(const t5_driver_offline_api_v1 *local, const t5_driver_manager_api_v1 *remote,
                     const t5_ui_api_v1 *ui, int32_t selected) {
    if (selected < 0 || selected >= (int32_t)row_count) return;
    if (selected == 0) {
        view = view == VIEW_LOCAL ? VIEW_ONLINE : VIEW_LOCAL;
        if (view == VIEW_LOCAL) refresh_local(local);
        else if (!online_loaded) refresh_online(remote, ui, local);
        else build_online(remote);
        return;
    }
    if (selected == 1) {
        if (view == VIEW_LOCAL) refresh_local(local);
        else refresh_online(remote, ui, local);
        return;
    }
    const uint32_t index = (uint32_t)selected - 2;
    if (view == VIEW_LOCAL) {
        const t5_driver_local_entry_t entry = local_items[index];
        if (entry.source != T5_DRIVER_LOCAL_INBOX || !(entry.flags & T5_DRIVER_LOCAL_VALID)) {
            snprintf(status, sizeof(status), "%s: %s", entry.id,
                     entry.flags & T5_DRIVER_LOCAL_VALID ? "Installed; activation unchanged" : "Invalid package; cannot install");
            return;
        }
        snprintf(status, sizeof(status), "Installing %s from SD...", entry.id);
        render(ui, local, remote, selected);
        const bool ok = local->install(index);
        build_local(local);
        snprintf(status, sizeof(status), "%s: %s", entry.id,
                 ok ? "Installed from SD; activation unchanged" : "Install rejected; previous driver retained");
        return;
    }
    if (!online_loaded || !has_online(remote)) return;
    const t5_driver_catalog_entry_t entry = remote_items[index];
    char installed[T5_DRIVER_VERSION_MAX] = {0};
    if (remote->installed_version_get(entry.id, installed, sizeof(installed)) &&
        strcmp(installed, entry.version) == 0) {
        snprintf(status, sizeof(status), "%s already current", entry.id);
        return;
    }
    snprintf(status, sizeof(status), "Installing %s from release...", entry.id);
    render(ui, local, remote, selected);
    const bool ok = remote->install(index);
    build_online(remote);
    snprintf(status, sizeof(status), "%s: %s", entry.id,
             ok ? "Installed; activation unchanged" : "Download or install failed");
}

__attribute__((visibility("default"))) void app_main(void) {
    const t5_app_api_v1 *app = t5_app_get_api(T5_APP_ABI_VERSION);
    const t5_ui_api_v1 *ui = t5_ui_get_api(T5_UI_API_VERSION);
    const t5_driver_offline_api_v1 *local = t5_driver_offline_get_api(T5_DRIVER_OFFLINE_API_VERSION);
    const t5_driver_manager_api_v1 *remote = t5_driver_manager_get_api(T5_DRIVER_MANAGER_API_VERSION);
    if (!app || !app->set_back_exits_app || !has_ui(ui) || !has_offline(local)) return;
    app->set_back_exits_app(false);
    view = VIEW_LOCAL;
    online_loaded = false;
    refresh_local(local);  // Never performs Wi-Fi I/O on application startup.
    int32_t selected = 0;
    render(ui, local, remote, selected);
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
                activate(local, remote, ui, selected);
                selected = 0; redraw = true; break;
            case T5_UI_EVENT_TAP: {
                const int32_t hit = ui->hit_test(event.touch_x, event.touch_y);
                if (hit >= 0 && hit < (int32_t)row_count) {
                    if (hit == selected) { activate(local, remote, ui, selected); selected = 0; }
                    else { selected = hit; status[0] = '\0'; }
                    redraw = true;
                } else if (hit == T5_UI_HIT_HEADER) {
                    activate(local, remote, ui, 1); selected = 0; redraw = true;
                }
                break;
            }
            case T5_UI_EVENT_BACK:
            case T5_UI_EVENT_EXIT:
                app->set_back_exits_app(true); return;
            default: break;
        }
        if (row_count <= 2) selected = 0;
        else if (selected >= (int32_t)row_count) selected = (int32_t)row_count - 1;
        if (redraw) render(ui, local, remote, selected);
    }
    app->set_back_exits_app(true);
}
