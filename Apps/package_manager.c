#include "T5AppApi.h"
#include "T5PackageManagerApi.h"
#include "T5PackageVersion.h"
#include "T5UiApi.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_ITEMS 64u
#define TITLE_BYTES 88u
#define SUBTITLE_BYTES 128u
#define STATUS_BYTES 160u

static t5_package_preview_t packages[MAX_ITEMS];
static t5_ui_list_row_t rows[MAX_ITEMS];
static char titles[MAX_ITEMS][TITLE_BYTES];
static char subtitles[MAX_ITEMS][SUBTITLE_BYTES];
static char values[MAX_ITEMS][T5_PACKAGE_VERSION_MAX];
static uint32_t row_count;

static bool api_ready(const t5_package_manager_api_v1 *packages_api,
                      const t5_ui_api_v1 *ui) {
    return packages_api && packages_api->api_version == T5_PACKAGE_MANAGER_API_VERSION &&
           packages_api->struct_size >= sizeof(t5_package_manager_api_v1) &&
           packages_api->preview && packages_api->install && packages_api->uninstall &&
           ui && ui->api_version == T5_UI_API_VERSION &&
           ui->struct_size >= offsetof(t5_ui_api_v1, previous_index) + sizeof(ui->previous_index) &&
           ui->render_list && ui->poll_event && ui->hit_test &&
           ui->next_index && ui->previous_index;
}
static const char *kind_name(uint8_t kind) {
    switch (kind) {
        case T5_PACKAGE_APPLICATION: return "App";
        case T5_PACKAGE_DRIVER: return "Driver";
        case T5_PACKAGE_SERVICE: return "Service";
        case T5_PACKAGE_PROVIDER: return "Provider";
        default: return "Invalid";
    }
}
static void refresh(const t5_app_api_v1 *app,
                    const t5_package_manager_api_v1 *manager) {
    row_count = 0;
    if (!app->dir_open("/sd/Packages/Inbox")) return;
    t5_app_dirent_t entry = {0};
    while (app->dir_next(&entry)) {
        if (!entry.is_directory || row_count >= MAX_ITEMS) continue;
        t5_package_preview_t info = {0};
        if (!manager->preview(entry.name, &info)) continue;
        packages[row_count] = info;
        snprintf(titles[row_count], sizeof(titles[row_count]), "%s [%s]",
                 info.id, kind_name(info.kind));
        if (!info.valid_installation) {
            snprintf(subtitles[row_count], sizeof(subtitles[row_count]),
                     "Installed generation needs recovery; no mutation");
        } else if (info.installed_version[0]) {
            snprintf(subtitles[row_count], sizeof(subtitles[row_count]),
                     "Installed %s; %s", info.installed_version,
                     info.install_allowed ? "update available" : "install unchanged");
        } else {
            snprintf(subtitles[row_count], sizeof(subtitles[row_count]), "%s",
                     info.install_allowed ? "SD inbox: ready to install" :
                     "Unavailable: dependency, version or pending stage");
        }
        snprintf(values[row_count], sizeof(values[row_count]), "%s", info.version);
        rows[row_count] = (t5_ui_list_row_t){titles[row_count], subtitles[row_count],
                                            values[row_count],
                                            info.install_allowed ? T5_UI_LIST_HIGHLIGHT_VALUE : 0};
        ++row_count;
    }
    app->dir_close();
}
static void render(const t5_ui_api_v1 *ui, int32_t selected, const char *status) {
    const t5_ui_chrome_t chrome = {
        .title = "Package Manager",
        .subtitle = "Offline SD: /Packages/Inbox/<id>",
        .status = status ? status : "",
        .back_label = "Back", .confirm_label = row_count ? "Actions" : "",
        .previous_label = "Up", .next_label = "Down",
    };
    if (row_count) ui->render_list(&chrome, rows, row_count, selected);
    else {
        const t5_ui_list_row_t empty = {
            "No packages in SD inbox", "Add <id>/.package.json and declared files",
            "Offline", 0,
        };
        ui->render_list(&chrome, &empty, 1, 0);
    }
}
static bool choose(const t5_ui_api_v1 *ui, const char* heading,
                   const char* description, const char* action) {
    const t5_ui_list_row_t options[2] = {
        {"Cancel", "Keep installed files and staged content", "Back", 0},
        {action, description, "Confirm", T5_UI_LIST_HIGHLIGHT_VALUE},
    };
    const t5_ui_chrome_t chrome = {
        .title = heading, .subtitle = description, .status = "No changes until confirmed",
        .back_label = "Cancel", .confirm_label = "Select",
        .previous_label = "Up", .next_label = "Down",
    };
    int32_t selected = 0;
    ui->render_list(&chrome, options, 2, selected);
    for (;;) {
        t5_ui_event_t event = {0};
        if (!ui->poll_event(&event, 20) || event.type == T5_UI_EVENT_BACK ||
            event.type == T5_UI_EVENT_EXIT) return false;
        if (event.type == T5_UI_EVENT_NEXT)
            selected = ui->next_index(selected, 2);
        if (event.type == T5_UI_EVENT_PREVIOUS)
            selected = ui->previous_index(selected, 2);
        if (event.type == T5_UI_EVENT_TAP) {
            const int32_t hit = ui->hit_test(event.touch_x, event.touch_y);
            if (hit < 0 || hit > 1) continue;
            selected = hit;
        }
        if (event.type == T5_UI_EVENT_CONFIRM || event.type == T5_UI_EVENT_TAP) {
            if (selected == 1) return true;
            return false;
        }
        ui->render_list(&chrome, options, 2, selected);
    }
}
static void activate(const t5_package_manager_api_v1 *manager,
                     const t5_ui_api_v1 *ui, int32_t selected,
                     char *status, size_t capacity) {
    if (!status || !capacity || selected < 0 || selected >= (int32_t)row_count) return;
    const t5_package_preview_t info = packages[selected];
    if (!info.valid_installation) {
        snprintf(status, capacity, "%s: recover invalid generation before changing it", info.id);
        return;
    }
    const bool installed = info.installed_version[0] != 0;
    if (info.install_allowed &&
        choose(ui, installed ? "Confirm package update" : "Confirm package install",
               "Integrity checked; no ELF activation or capability grant",
               installed ? "Update package" : "Install package")) {
        const bool okay = manager->install(info.id);
        snprintf(status, capacity, "%s: %s; no activation", info.id,
                 okay ? "installed" : "install refused; inspect SD/recovery");
        return;
    }
    // Uninstall is a separate user decision and an additional confirmation;
    // cancelled installs do not automatically initiate a destructive action.
    if (installed && choose(ui, "Uninstall package?",
                            "Stop users first; no legacy files are removed",
                            "Continue to uninstall") &&
        choose(ui, "Final uninstall confirmation",
               "Delete the selected installed package; cannot be undone",
               "Uninstall now")) {
        const bool okay = manager->uninstall(info.kind, info.id);
        snprintf(status, capacity, "%s: %s", info.id,
                 okay ? "uninstalled" : "uninstall refused; stop mapped users");
    }
}

__attribute__((visibility("default"))) void app_main(void) {
    const t5_app_api_v1 *app = t5_app_get_api(T5_APP_ABI_VERSION);
    const t5_package_manager_api_v1 *manager =
        t5_package_manager_get_api(T5_PACKAGE_MANAGER_API_VERSION);
    const t5_ui_api_v1 *ui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!app || !app->dir_open || !app->dir_next || !app->dir_close ||
        !app->set_back_exits_app || !api_ready(manager, ui)) return;
    app->set_back_exits_app(false);
    refresh(app, manager);
    int32_t selected = 0;
    char status[STATUS_BYTES] = {0};
    render(ui, selected, status);
    for (;;) {
        t5_ui_event_t event = {0};
        if (!ui->poll_event(&event, 20)) break;
        bool redraw = false;
        switch (event.type) {
            case T5_UI_EVENT_PREVIOUS:
                selected = ui->previous_index(selected, row_count);
                redraw = true;
                break;
            case T5_UI_EVENT_NEXT:
                selected = ui->next_index(selected, row_count);
                redraw = true;
                break;
            case T5_UI_EVENT_CONFIRM:
                activate(manager, ui, selected, status, sizeof(status));
                refresh(app, manager);
                redraw = true;
                break;
            case T5_UI_EVENT_TAP: {
                const int32_t hit = ui->hit_test(event.touch_x, event.touch_y);
                if (hit >= 0 && hit < (int32_t)row_count) {
                    if (hit == selected) activate(manager, ui, selected, status, sizeof(status));
                    selected = hit;
                    refresh(app, manager);
                    redraw = true;
                } else if (hit == T5_UI_HIT_HEADER) {
                    refresh(app, manager);
                    selected = 0;
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
        if (!row_count || selected >= (int32_t)row_count) selected = 0;
        if (redraw) render(ui, selected, status);
    }
    app->set_back_exits_app(true);
}
