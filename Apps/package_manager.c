#include "T5AppApi.h"
#include "T5PackageManagerApi.h"
#include "T5UiApi.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_ITEMS 64u
#define TITLE_BYTES 88u
#define SUBTITLE_BYTES 160u
#define STATUS_BYTES 192u
#define INBOX_NAME_BYTES 128u

typedef enum {
    PACKAGE_ROW_INSTALLED = 1,
    PACKAGE_ROW_INBOX = 2,
} package_row_source_t;

typedef struct {
    uint8_t source;
    uint8_t has_staged;
    uint8_t reserved[2];
    t5_installed_package_t installed;
    t5_package_preview_t staged;
} package_row_t;

typedef enum {
    PACKAGE_ACTION_NONE = 0,
    PACKAGE_ACTION_INSTALL = 1,
    PACKAGE_ACTION_REPLACE = 2,
    PACKAGE_ACTION_UNINSTALL = 3,
} package_action_t;

static package_row_t packages[MAX_ITEMS];
static t5_ui_list_row_t rows[MAX_ITEMS];
static char titles[MAX_ITEMS][TITLE_BYTES];
static char subtitles[MAX_ITEMS][SUBTITLE_BYTES];
static char values[MAX_ITEMS][T5_PACKAGE_VERSION_MAX];
static char names[MAX_ITEMS][INBOX_NAME_BYTES];
static bool archive_rows[MAX_ITEMS];
static uint32_t online_indices[MAX_ITEMS];
static uint32_t row_count;
static bool online_view;

static bool api_ready(const t5_package_manager_api_v1 *manager,
                      const t5_ui_api_v1 *ui) {
    const size_t manager_required =
        offsetof(t5_package_manager_api_v1, replace) + sizeof(manager->replace);
    return manager && manager->api_version == T5_PACKAGE_MANAGER_API_VERSION &&
           manager->struct_size >= manager_required &&
           manager->preview && manager->install && manager->uninstall &&
           manager->installed_refresh && manager->installed_count &&
           manager->installed_get && manager->replace &&
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
static bool online_available(const t5_package_manager_api_v1 *manager) {
    return manager->struct_size >= offsetof(t5_package_manager_api_v1, online_install) +
        sizeof(manager->online_install) && manager->online_refresh &&
        manager->online_count && manager->online_get && manager->online_install;
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
static bool parse_version(const char *text, uint32_t parts[3]) {
    size_t component = 0;
    if (!text || !parts) return false;
    parts[0] = parts[1] = parts[2] = 0;
    if (!*text) return false;
    while (*text) {
        if (*text == '.') {
            if (++component >= 3u) return false;
            ++text;
            continue;
        }
        if (*text < '0' || *text > '9') return false;
        const uint32_t digit = (uint32_t)(*text - '0');
        if (parts[component] > (UINT32_MAX - digit) / 10u) return false;
        parts[component] = parts[component] * 10u + digit;
        ++text;
    }
    return component == 2u;
}

static int version_order(const char *candidate, const char *installed) {
    uint32_t a[3], b[3];
    size_t i;
    if (!parse_version(candidate, a) || !parse_version(installed, b)) return 0;
    for (i = 0; i < 3u; ++i) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

static bool staged_matches_installed(const t5_package_preview_t *staged,
                                     const t5_installed_package_t *installed) {
    return staged && installed &&
           staged->kind == installed->kind &&
           strcmp(staged->id, installed->id) == 0;
}

static int32_t find_row(uint8_t kind, const char *id) {
    uint32_t i;
    for (i = 0; i < row_count; ++i) {
        const package_row_t *item = &packages[i];
        if (item->source == PACKAGE_ROW_INSTALLED &&
            item->installed.kind == kind && strcmp(item->installed.id, id) == 0)
            return (int32_t)i;
        if (item->source == PACKAGE_ROW_INBOX &&
            item->staged.kind == kind && strcmp(item->staged.id, id) == 0)
            return (int32_t)i;
    }
    return -1;
}

static void format_installed_row(uint32_t index) {
    package_row_t *item = &packages[index];
    const t5_installed_package_t *installed = &item->installed;
    snprintf(titles[index], sizeof(titles[index]), "%s [%s]",
             installed->id, kind_name(installed->kind));
    if (!installed->valid_installation) {
        snprintf(subtitles[index], sizeof(subtitles[index]),
                 "Installed generation is invalid; recovery required before mutation");
        snprintf(values[index], sizeof(values[index]), "%s", "Invalid");
    } else if (item->has_staged) {
        const int order = version_order(item->staged.version, installed->version);
        snprintf(subtitles[index], sizeof(subtitles[index]),
                 "Installed %s; staged %s (%s)",
                 installed->version, item->staged.version,
                 order < 0 ? "older" : order > 0 ? "newer" : "same version");
        snprintf(values[index], sizeof(values[index]), "%s", installed->version);
    } else {
        snprintf(subtitles[index], sizeof(subtitles[index]),
                 "Installed %s; no staged replacement", installed->version);
        snprintf(values[index], sizeof(values[index]), "%s", installed->version);
    }
    rows[index] = (t5_ui_list_row_t){
        titles[index], subtitles[index], values[index],
        item->has_staged && installed->valid_installation &&
                version_order(item->staged.version, installed->version) != 0
            ? T5_UI_LIST_HIGHLIGHT_VALUE : 0
    };
}

static void format_inbox_row(uint32_t index) {
    package_row_t *item = &packages[index];
    const t5_package_preview_t *staged = &item->staged;
    snprintf(titles[index], sizeof(titles[index]), "%s [%s]",
             staged->id, kind_name(staged->kind));
    snprintf(subtitles[index], sizeof(subtitles[index]), "%s",
             staged->install_allowed ? "Inbox package ready for fresh installation" :
             "Inbox package unavailable: dependency, version, stage, or active mapping");
    snprintf(values[index], sizeof(values[index]), "%s", staged->version);
    rows[index] = (t5_ui_list_row_t){
        titles[index], subtitles[index], values[index],
        staged->install_allowed ? T5_UI_LIST_HIGHLIGHT_VALUE : 0
    };
}

static void set_row(uint32_t index, const t5_package_preview_t *info,
                    const char *available) {
    package_row_t *item = &packages[index];
    memset(item, 0, sizeof(*item));
    item->source = PACKAGE_ROW_INBOX;
    item->has_staged = 1;
    item->staged = *info;
    snprintf(titles[index], sizeof(titles[index]), "%.63s [%s]",
             info->id, kind_name(info->kind));
    if (!info->valid_installation)
        snprintf(subtitles[index], sizeof(subtitles[index]),
                 "Installed generation needs recovery; no mutation");
    else if (info->installed_version[0])
        snprintf(subtitles[index], sizeof(subtitles[index]),
                 "Installed %.31s; %s", info->installed_version,
                 info->install_allowed ? "update available" : "no update");
    else
        snprintf(subtitles[index], sizeof(subtitles[index]), "%s",
                 info->install_allowed ? available :
                 "Unavailable: dependency, version or pending stage");
    snprintf(values[index], sizeof(values[index]), "%s", info->version);
    rows[index] = (t5_ui_list_row_t){titles[index], subtitles[index], values[index],
                                    info->install_allowed ? T5_UI_LIST_HIGHLIGHT_VALUE : 0};
}
static void refresh(const t5_app_api_v1 *app,
                    const t5_package_manager_api_v1 *manager) {
    uint32_t i;
    row_count = 0;
    memset(packages, 0, sizeof(packages));

    if (manager->installed_refresh()) {
        const uint32_t installed_count = manager->installed_count();
        for (i = 0; i < installed_count && row_count < MAX_ITEMS; ++i) {
            t5_installed_package_t installed = {0};
            if (!manager->installed_get(i, &installed)) continue;
            package_row_t *item = &packages[row_count];
            item->source = PACKAGE_ROW_INSTALLED;
            item->installed = installed;
            t5_package_preview_t staged = {0};
            if (manager->preview(installed.id, &staged) &&
                staged_matches_installed(&staged, &installed)) {
                item->has_staged = 1;
                item->staged = staged;
            }
            format_installed_row(row_count);
            ++row_count;
        }
    }

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
        set_row(row_count, &info, archive ? "SD ZIP: ready to install" :
                                           "SD directory: ready to install");
        ++row_count;
    }
    app->dir_close();
}
static bool refresh_online(const t5_package_manager_api_v1 *manager) {
    row_count = 0;
    if (!online_available(manager) || !manager->online_refresh()) return false;
    const uint32_t count = manager->online_count();
    for (uint32_t i = 0; i < count && row_count < MAX_ITEMS; ++i) {
        t5_package_catalog_row_t item = {0};
        if (!manager->online_get(i, &item)) continue;
        online_indices[row_count] = i;
        set_row(row_count, &item.package, "Release ZIP: ready to install");
        ++row_count;
    }
    return true;
}
static void refresh_current(const t5_app_api_v1 *app,
                            const t5_package_manager_api_v1 *manager) {
    if (online_view) {
        // Do not change the selected release while a user is on its list.
        // Refresh explicitly when entering online view, not after an install.
        row_count = 0;
        const uint32_t count = manager->online_count();
        for (uint32_t i = 0; i < count && row_count < MAX_ITEMS; ++i) {
            t5_package_catalog_row_t item = {0};
            if (!manager->online_get(i, &item)) continue;
            online_indices[row_count] = i;
            set_row(row_count, &item.package, "Release ZIP: ready to install");
            ++row_count;
        }
    } else refresh(app, manager);
}
static void render(const t5_ui_api_v1 *ui, int32_t selected, const char *status) {
    const t5_ui_chrome_t chrome = {
        .title = "Package Manager",
        .subtitle = online_view ? "Pinned release ZIPs; tap header for SD" :
                    "Offline /Packages/Inbox; tap header for online",
        .status = status ? status : "",
        .back_label = "Back", .confirm_label = row_count ? "Manage" : "",
        .previous_label = "Up", .next_label = "Down",
    };
    if (row_count) {
        ui->render_list(&chrome, rows, row_count, selected);
    } else {
        const t5_ui_list_row_t empty = {
            online_view ? "No release packages" : "No packages in SD inbox",
            online_view ? "Check saved Wi-Fi or generic package catalog" :
                "Add a .rte.zip or <id>/.package.json", online_view ? "Online" : "Offline", 0,
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
        {"Cancel", "Keep the installed package unchanged", "Back", 0},
        {action, description, "Confirm", T5_UI_LIST_HIGHLIGHT_VALUE},
    };
    return menu(ui, heading, description, choices, 2) == 1;
}

static package_action_t select_action(const t5_ui_api_v1 *ui,
                                      const package_row_t *item) {
    t5_ui_list_row_t actions[3] = {
        {"Cancel", "Return to installed package list", "Back", 0},
        {"", "", "", 0},
        {"", "", "", 0},
    };
    char replacement_title[64] = {0};
    char replacement_subtitle[128] = {0};
    char replacement_value[T5_PACKAGE_VERSION_MAX] = {0};
    uint32_t count = 1;
    int32_t replace_index = -1, install_index = -1, uninstall_index = -1;

    if (item->source == PACKAGE_ROW_INSTALLED) {
        if (item->installed.valid_installation && item->has_staged &&
            item->staged.valid_installation &&
            staged_matches_installed(&item->staged, &item->installed)) {
            const int order = version_order(item->staged.version, item->installed.version);
            if (order != 0) {
                replace_index = (int32_t)count;
                snprintf(replacement_title, sizeof(replacement_title), "%s",
                         order < 0 ? "Downgrade to staged version" : "Replace with staged version");
                snprintf(replacement_subtitle, sizeof(replacement_subtitle),
                         "Installed %s -> staged %s", item->installed.version, item->staged.version);
                snprintf(replacement_value, sizeof(replacement_value), "%s", item->staged.version);
                actions[count++] = (t5_ui_list_row_t){
                    replacement_title, replacement_subtitle, replacement_value,
                    T5_UI_LIST_HIGHLIGHT_VALUE};
            }
        }
        if (item->installed.valid_installation) {
            uninstall_index = (int32_t)count;
            actions[count++] = (t5_ui_list_row_t){
                "Uninstall package", "Remove this installed managed generation",
                "Remove", 0};
        }
    } else if (item->source == PACKAGE_ROW_INBOX && item->staged.install_allowed) {
        install_index = (int32_t)count;
        actions[count++] = (t5_ui_list_row_t){
            "Install package", "Publish verified staged bytes; no activation",
            "Install", T5_UI_LIST_HIGHLIGHT_VALUE};
    }

    const char *id = item->source == PACKAGE_ROW_INSTALLED ?
        item->installed.id : item->staged.id;
    const int32_t choice = menu(ui, "Package actions", id, actions, count);
    if (choice == replace_index) return PACKAGE_ACTION_REPLACE;
    if (choice == install_index) return PACKAGE_ACTION_INSTALL;
    if (choice == uninstall_index) return PACKAGE_ACTION_UNINSTALL;
    return PACKAGE_ACTION_NONE;
}

static void activate(const t5_package_manager_api_v1 *manager,
                     const t5_ui_api_v1 *ui, int32_t selected,
                     char *status, size_t capacity) {
    if (!status || !capacity || selected < 0 || selected >= (int32_t)row_count) return;
    const package_row_t item = packages[selected];

    if (item.source == PACKAGE_ROW_INSTALLED && !item.installed.valid_installation) {
        snprintf(status, capacity, "%s: invalid installed generation; recovery required",
                 item.installed.id);
        return;
    }

    const package_action_t action = select_action(ui, &item);
    if (action == PACKAGE_ACTION_INSTALL) {
        if (confirm(ui, "Confirm installation",
                    "Install verified package; no activation or privilege grant",
                    "Install now")) {
            bool okay = false;
            if (online_view)
                okay = manager->online_install(online_indices[selected]);
            else if (archive_rows[selected])
                okay = manager->install_archive(names[selected]);
            else
                okay = manager->install(item.staged.id);
            snprintf(status, capacity, "%s: %s", item.staged.id,
                     okay ? "installed" : "install refused; inspect package/recovery");
        }
        return;
    }

    if (action == PACKAGE_ACTION_REPLACE) {
        const int order = version_order(item.staged.version, item.installed.version);
        char description[224];
        snprintf(description, sizeof(description),
                 "%s %s %s -> %s; verified replacement only",
                 order < 0 ? "Downgrade" : "Replace",
                 item.installed.id, item.installed.version, item.staged.version);
        if (confirm(ui, order < 0 ? "Confirm downgrade" : "Confirm replacement",
                    description, order < 0 ? "Downgrade now" : "Replace now")) {
            const bool okay = manager->replace(item.staged.id);
            snprintf(status, capacity, "%s: %s %s -> %s",
                     item.installed.id,
                     okay ? (order < 0 ? "downgraded" : "replaced") : "replacement refused",
                     item.installed.version, item.staged.version);
        }
        return;
    }

    if (action == PACKAGE_ACTION_UNINSTALL &&
        confirm(ui, "Confirm uninstall",
                "Remove selected package; active/mapped packages are refused",
                "Uninstall now")) {
        const bool okay = manager->uninstall(item.installed.kind, item.installed.id);
        snprintf(status, capacity, "%s: %s", item.installed.id,
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
    online_view = false;
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
                refresh_current(app, manager);
                redraw = true;
                break;
            case T5_UI_EVENT_TAP: {
                const int32_t hit = ui->hit_test(event.touch_x, event.touch_y);
                if (hit >= 0 && hit < (int32_t)row_count) {
                    if (hit == selected) activate(manager, ui, selected, status, sizeof(status));
                    selected = hit;
                    refresh_current(app, manager);
                    redraw = true;
                } else if (hit == T5_UI_HIT_HEADER) {
                    if (!online_view && online_available(manager)) {
                        online_view = true;
                        if (!refresh_online(manager))
                            snprintf(status, sizeof(status), "Online catalog unavailable; SD still accessible");
                        else status[0] = '\0';
                    } else {
                        online_view = false;
                        refresh(app, manager);
                        status[0] = '\0';
                    }
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
