#include "T5AppApi.h"
#include "T5PackageManagerApi.h"
#include "T5UiApi.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_ITEMS 64u
#define TITLE_BYTES 88u
#define SUBTITLE_BYTES 128u
#define STATUS_BYTES 160u
#define INBOX_NAME_BYTES 128u

static t5_package_preview_t packages[MAX_ITEMS];
static t5_ui_list_row_t rows[MAX_ITEMS];
static char titles[MAX_ITEMS][TITLE_BYTES];
static char subtitles[MAX_ITEMS][SUBTITLE_BYTES];
static char values[MAX_ITEMS][T5_PACKAGE_VERSION_MAX];
static char names[MAX_ITEMS][INBOX_NAME_BYTES];
static bool archive_rows[MAX_ITEMS];
static uint32_t row_count;

static bool api_ready(const t5_package_manager_api_v1 *manager,
                      const t5_ui_api_v1 *ui) {
    return manager && manager->api_version == T5_PACKAGE_MANAGER_API_VERSION &&
           manager->struct_size >= offsetof(t5_package_manager_api_v1, preview_archive) &&
           manager->preview && manager->install && manager->uninstall &&
           ui && ui->api_version == T5_UI_API_VERSION &&
           ui->struct_size >= offsetof(t5_ui_api_v1, previous_index) + sizeof(ui->previous_index) &&
           ui->render_list && ui->poll_event && ui->hit_test &&
           ui->next_index && ui->previous_index;
}
static bool zip_available(const t5_package_manager_api_v1 *manager) {
    return manager->struct_size >= offsetof(t5_package_manager_api_v1, install_archive) +
           sizeof(manager->install_archive) && manager->preview_archive &&
           manager->install_archive;
}
static size_t bounded_length(const char *value, size_t capacity) {
    size_t used = 0;
    if (value) while (used < capacity && value[used]) ++used;
    return used;
}
static bool zip_name(const char *name) {
    if (!name) return false;
    const size_t n = bounded_length(name, INBOX_NAME_BYTES);
    return n > 8 && n < INBOX_NAME_BYTES && !strcmp(name + n - 8, ".rte.zip");
}
static const char* kind_name(uint8_t kind) {
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
        if (row_count >= MAX_ITEMS) break;
        const size_t n = bounded_length(entry.name, sizeof(entry.name));
        if (!n || n >= sizeof(entry.name) || n >= INBOX_NAME_BYTES) continue;
        const bool archive = !entry.is_directory && zip_name(entry.name) &&
                             zip_available(manager);
        if (!entry.is_directory && !archive) continue;
        t5_package_preview_t info = {0};
        const bool described = archive ? manager->preview_archive(entry.name, &info) :
                                        manager->preview(entry.name, &info);
        if (!described) continue;
        memcpy(names[row_count], entry.name, n + 1u);
        archive_rows[row_count] = archive;
        packages[row_count] = info;
        snprintf(titles[row_count], sizeof(titles[row_count]), "%s [%s]",
                 info.id, kind_name(info.kind));
        if (!info.valid_installation)
            snprintf(subtitles[row_count], sizeof(subtitles[row_count]),
                     "Installed generation needs recovery; no mutation");
        else if (info.installed_version[0])
            snprintf(subtitles[row_count], sizeof(subtitles[row_count]),
                     "Installed %s; %s", info.installed_version,
                     info.install_allowed ? "update available" : "no update");
        else
            snprintf(subtitles[row_count], sizeof(subtitles[row_count]), "%s",
                     info.install_allowed ? (archive ? "SD ZIP: ready to install" :
                                                  "SD directory: ready to install") :
                     "Unavailable: dependency, version or pending stage");
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
        .subtitle = "Offline SD: /Packages/Inbox (ZIP or directory)",
        .status = status ? status : "",
        .back_label = "Back", .confirm_label = row_count ? "Actions" : "",
        .previous_label = "Up", .next_label = "Down",
    };
    if (row_count) ui->render_list(&chrome, rows, row_count, selected);
    else {
        const t5_ui_list_row_t empty = {
            "No packages in SD inbox", "Add a .rte.zip or <id>/.package.json",
            "Offline", 0,
        };
        ui->render_list(&chrome, &empty, 1, 0);
    }
}

// All selectors default to cancel. Back/Exit and failed polling never mutate.
static int32_t menu(const t5_ui_api_v1 *ui, const char* title,
                    const char* description, const t5_ui_list_row_t *items,
                    uint32_t count) {
    if (!count) return 0;
    const t5_ui_chrome_t chrome = {
        .title = title, .subtitle = description,
        .status = "No change until Confirm", .back_label = "Cancel",
        .confirm_label = "Select", .previous_label = "Up", .next_label = "Down",
    };
    int32_t selected = 0;
    ui->render_list(&chrome, items, count, selected);
    for (;;) {
        t5_ui_event_t event = {0};
        if (!ui->poll_event(&event, 20) || event.type == T5_UI_EVENT_BACK ||
            event.type == T5_UI_EVENT_EXIT) return 0;
        if (event.type == T5_UI_EVENT_NEXT) selected = ui->next_index(selected, count);
        if (event.type == T5_UI_EVENT_PREVIOUS) selected = ui->previous_index(selected, count);
        if (event.type == T5_UI_EVENT_TAP) {
            const int32_t hit = ui->hit_test(event.touch_x, event.touch_y);
            if (hit < 0 || hit >= (int32_t)count) continue;
            selected = hit;
        }
        if (event.type == T5_UI_EVENT_CONFIRM || event.type == T5_UI_EVENT_TAP)
            return selected;
        ui->render_list(&chrome, items, count, selected);
    }
}
static bool confirm(const t5_ui_api_v1 *ui, const char *heading,
                    const char *description, const char *action) {
    const t5_ui_list_row_t choices[2] = {
        {"Cancel", "Keep all installed and staged files", "Back", 0},
        {action, description, "Confirm", T5_UI_LIST_HIGHLIGHT_VALUE},
    };
    return menu(ui, heading, description, choices, 2) == 1;
}
static int32_t select_action(const t5_ui_api_v1 *ui,
                             const t5_package_preview_t *info) {
    t5_ui_list_row_t actions[3] = {
        {"Cancel", "Return to package list", "Back", 0},
        {"", "", "", 0},
        {"", "", "", 0},
    };
    uint32_t count = 1;
    int32_t installIndex = -1, uninstallIndex = -1;
    if (info->install_allowed) {
        installIndex = (int32_t)count;
        actions[count++] = (t5_ui_list_row_t){
            info->installed_version[0] ? "Update package" : "Install package",
            "Publish verified bytes; does not activate ELF", "Install", 0};
    }
    if (info->installed_version[0]) {
        uninstallIndex = (int32_t)count;
        actions[count++] = (t5_ui_list_row_t){
            "Uninstall package", "Requires inactive ELF; removes this version",
            "Remove", 0};
    }
    const int32_t choice = menu(ui, "Package actions", info->id, actions, count);
    return choice == installIndex ? 1 : choice == uninstallIndex ? 2 : 0;
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
    const int32_t action = select_action(ui, &info);
    if (action == 1 &&
        confirm(ui, "Confirm installation",
                "Integrity checked; no activation or privilege grant", "Install now")) {
        const bool okay = archive_rows[selected] ?
            manager->install_archive(names[selected]) : manager->install(names[selected]);
        snprintf(status, capacity, "%s: %s; no activation", info.id,
                 okay ? "installed" : "install refused; inspect SD/recovery");
    } else if (action == 2 &&
               confirm(ui, "Confirm uninstall",
                       "Remove selected package; cannot be undone", "Uninstall now")) {
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
