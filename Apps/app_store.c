#include "T5AppApi.h"
#include "T5PackageManagerApi.h"
#include "T5UiApi.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_ROWS 64u
#define TITLE_SIZE 96u
#define SUBTITLE_SIZE 96u
#define VALUE_SIZE 40u
#define STATUS_SIZE 160u
#define INBOX_NAME_SIZE 128u

typedef enum { RELEASES, SD_INBOX } view_t;
static view_t view;
static t5_ui_list_row_t rows[MAX_ROWS];
static char titles[MAX_ROWS][TITLE_SIZE];
static char subtitles[MAX_ROWS][SUBTITLE_SIZE];
static char values[MAX_ROWS][VALUE_SIZE];
static char folders[MAX_ROWS][INBOX_NAME_SIZE];
static bool archive_rows[MAX_ROWS];
static t5_package_preview_t packages[MAX_ROWS];
static uint32_t release_indices[MAX_ROWS];
static uint32_t row_count;

static size_t bounded_length(const char *value, size_t bound) {
    size_t n = 0;
    if (value) while (n < bound && value[n]) ++n;
    return n;
}
static void copy_text(char *destination, size_t capacity, const char *source) {
    if (!destination || !capacity) return;
    if (!source) source = "";
    const size_t length = bounded_length(source, capacity - 1u);
    memcpy(destination, source, length);
    destination[length] = '\0';
}
static bool zip_name(const char *name) {
    const size_t n = bounded_length(name, INBOX_NAME_SIZE);
    return n > 8u && n < INBOX_NAME_SIZE && !strcmp(name + n - 8u, ".rte.zip");
}
static bool package_available(const t5_package_manager_api_v1 *manager) {
    return manager && manager->api_version == T5_PACKAGE_MANAGER_API_VERSION &&
        manager->struct_size >= offsetof(t5_package_manager_api_v1, install_archive) +
            sizeof(manager->install_archive) && manager->preview && manager->install &&
        manager->uninstall && manager->preview_archive && manager->install_archive;
}
static bool online_available(const t5_package_manager_api_v1 *manager) {
    return package_available(manager) &&
        manager->struct_size >= offsetof(t5_package_manager_api_v1, online_install) +
            sizeof(manager->online_install) && manager->online_refresh &&
        manager->online_count && manager->online_get && manager->online_install;
}
static bool ui_available(const t5_ui_api_v1 *ui) {
    return ui && ui->struct_size >= offsetof(t5_ui_api_v1, previous_index) +
        sizeof(ui->previous_index) && ui->render_list && ui->hit_test &&
        ui->poll_event && ui->next_index && ui->previous_index;
}
static void row(uint32_t index, const char *title, const char *subtitle,
                const char *value, bool highlighted) {
    copy_text(titles[index], sizeof(titles[index]), title);
    copy_text(subtitles[index], sizeof(subtitles[index]), subtitle);
    copy_text(values[index], sizeof(values[index]), value);
    rows[index] = (t5_ui_list_row_t){titles[index], subtitles[index], values[index],
                                    highlighted ? T5_UI_LIST_HIGHLIGHT_VALUE : 0};
}
static void describe_row(uint32_t index, const t5_package_preview_t *info,
                         const char *ready_text) {
    char detail[SUBTITLE_SIZE] = {0};
    if (!info->valid_installation) {
        copy_text(detail, sizeof(detail), "Installed package needs recovery");
    } else if (info->installed_version[0]) {
        snprintf(detail, sizeof(detail), "Installed %.31s; %s", info->installed_version,
                 info->install_allowed ? "update available" : "current or blocked");
    } else {
        copy_text(detail, sizeof(detail), info->install_allowed ? ready_text :
                  "Dependency, version or stage blocked");
    }
    row(index, info->id, detail, info->version, info->install_allowed != 0);
}
static void build_releases(const t5_package_manager_api_v1 *manager) {
    row_count = 0;
    const uint32_t count = manager->online_count();
    for (uint32_t index = 0; index < count && row_count < MAX_ROWS; ++index) {
        t5_package_catalog_row_t catalog = {0};
        if (!manager->online_get(index, &catalog) ||
            catalog.package.kind != T5_PACKAGE_APPLICATION) continue;
        packages[row_count] = catalog.package;
        release_indices[row_count] = index;
        describe_row(row_count, &packages[row_count], "Verified release: ready to install");
        ++row_count;
    }
}
static bool build_inbox(const t5_app_api_v1 *app,
                        const t5_package_manager_api_v1 *manager) {
    row_count = 0;
    if (!app->dir_open("/sd/Packages/Inbox")) return false;
    t5_app_dirent_t entry = {0};
    while (app->dir_next(&entry)) {
        if (row_count == MAX_ROWS) break;
        const size_t length = bounded_length(entry.name, sizeof(entry.name));
        if (!length || length >= sizeof(entry.name) || length >= INBOX_NAME_SIZE) continue;
        const bool archive = !entry.is_directory && zip_name(entry.name);
        if (!entry.is_directory && !archive) continue;
        t5_package_preview_t info = {0};
        const bool described = archive ? manager->preview_archive(entry.name, &info) :
                                        manager->preview(entry.name, &info);
        if (!described || info.kind != T5_PACKAGE_APPLICATION) continue;
        memcpy(folders[row_count], entry.name, length + 1u);
        archive_rows[row_count] = archive;
        packages[row_count] = info;
        describe_row(row_count, &info, archive ? "SD ZIP: ready to install" :
                                                "SD directory: ready to install");
        ++row_count;
    }
    app->dir_close();
    return true;
}
static bool refresh_releases(const t5_package_manager_api_v1 *manager,
                             const t5_ui_api_v1 *ui) {
    const t5_ui_list_row_t loading = {"Loading package catalog", "Using saved Wi-Fi", "", 0};
    const t5_ui_chrome_t chrome = {"App Store", "Immutable release catalog", "Connecting...",
                                   "Back", "", "", ""};
    ui->render_list(&chrome, &loading, 1, 0);
    if (!manager->online_refresh()) return false;
    build_releases(manager);
    return true;
}
static bool removal_confirmed(const t5_ui_api_v1 *ui, const char *name) {
    const t5_ui_list_row_t choices[] = {
        {"Keep application", "Leave files unchanged", "Cancel", 0},
        {"Uninstall application", "Only if not in use", "Remove", T5_UI_LIST_HIGHLIGHT_VALUE},
    };
    const t5_ui_chrome_t chrome = {"Confirm removal", name, "Select Remove to uninstall",
                                   "Cancel", "Select", "Up", "Down"};
    int32_t selected = 0;
    ui->render_list(&chrome, choices, 2, selected);
    for (;;) {
        t5_ui_event_t event = {0};
        if (!ui->poll_event(&event, 20) || event.type == T5_UI_EVENT_BACK ||
            event.type == T5_UI_EVENT_EXIT) return false;
        if (event.type == T5_UI_EVENT_PREVIOUS) selected = ui->previous_index(selected, 2);
        else if (event.type == T5_UI_EVENT_NEXT) selected = ui->next_index(selected, 2);
        else if (event.type == T5_UI_EVENT_TAP) {
            const int32_t hit = ui->hit_test(event.touch_x, event.touch_y);
            if (hit >= 0 && hit < 2) selected = hit;
        } else if (event.type == T5_UI_EVENT_CONFIRM) return selected == 1;
        ui->render_list(&chrome, choices, 2, selected);
    }
}
static const char *action_label(int32_t selected) {
    if (selected < 0 || selected >= (int32_t)row_count) return "";
    const t5_package_preview_t *package = &packages[selected];
    if (!package->valid_installation) return "";
    if (package->install_allowed)
        return package->installed_version[0] ? "Update" : "Install";
    return view == SD_INBOX && package->installed_version[0] ? "Actions" : "";
}
static void render(const t5_ui_api_v1 *ui, int32_t selected, const char *status) {
    const t5_ui_chrome_t chrome = {
        .title = "App Store",
        .subtitle = view == SD_INBOX ? "SD ZIP/packages; tap header for releases" :
                    "Package catalog; tap header for SD packages",
        .status = status,
        .back_label = "Back", .confirm_label = action_label(selected),
        .previous_label = "Up", .next_label = "Down",
    };
    if (row_count) ui->render_list(&chrome, rows, row_count, selected);
    else {
        const t5_ui_list_row_t empty = {"No applications", view == SD_INBOX ?
            "Add .rte.zip or package under /Packages/Inbox" :
            "Package catalog unavailable", "", 0};
        ui->render_list(&chrome, &empty, 1, 0);
    }
}
static void activate_inbox(const t5_package_manager_api_v1 *manager,
                           const t5_ui_api_v1 *ui, int32_t selected,
                           char *status, size_t capacity) {
    const t5_package_preview_t package = packages[selected];
    if (!package.valid_installation) {
        snprintf(status, capacity, "%.63s: recovery required", package.id);
    } else if (package.install_allowed) {
        const bool ok = archive_rows[selected] ? manager->install_archive(folders[selected]) :
                                                manager->install(folders[selected]);
        snprintf(status, capacity, "%.63s: %s", package.id,
                 ok ? "verified package installed" : "install refused: check stage/dependencies");
    } else if (package.installed_version[0] && removal_confirmed(ui, package.id)) {
        const bool ok = manager->uninstall(T5_PACKAGE_APPLICATION, package.id);
        snprintf(status, capacity, "%.63s: %s", package.id,
                 ok ? "uninstalled" : "uninstall refused: active users");
    } else if (!package.installed_version[0]) {
        snprintf(status, capacity, "%.63s: dependency/version/stage blocked", package.id);
    }
}
static void activate_release(const t5_package_manager_api_v1 *manager,
                             const t5_ui_api_v1 *ui, int32_t selected,
                             char *status, size_t capacity) {
    const t5_package_preview_t package = packages[selected];
    if (!package.valid_installation) {
        snprintf(status, capacity, "%.63s: installed generation needs recovery", package.id);
        return;
    }
    if (!package.install_allowed) {
        snprintf(status, capacity, "%.63s: %s", package.id,
                 package.installed_version[0] ? "already current or blocked" :
                                                "dependency/version/stage blocked");
        return;
    }
    snprintf(status, capacity, "%s %.63s...",
             package.installed_version[0] ? "Updating" : "Installing", package.id);
    render(ui, selected, status);
    const bool okay = manager->online_install(release_indices[selected]);
    build_releases(manager);
    snprintf(status, capacity, "%.63s: %s", package.id,
             okay ? "verified package installed" : "installation failed verification");
}

__attribute__((visibility("default"))) void app_main(void) {
    const t5_app_api_v1 *app = t5_app_get_api(T5_APP_ABI_VERSION);
    const t5_package_manager_api_v1 *manager =
        t5_package_manager_get_api(T5_PACKAGE_MANAGER_API_VERSION);
    const t5_ui_api_v1 *ui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!app || !online_available(manager) || !ui_available(ui) ||
        !app->set_back_exits_app || !app->dir_open || !app->dir_next || !app->dir_close) return;
    app->set_back_exits_app(false);
    view = RELEASES;
    if (!refresh_releases(manager, ui)) {
        view = SD_INBOX;
        (void)build_inbox(app, manager);
    }
    int32_t selected = 0;
    char status[STATUS_SIZE] = {0};
    render(ui, selected, status);
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
                if (row_count && selected >= 0 && selected < (int32_t)row_count) {
                    if (view == SD_INBOX) {
                        activate_inbox(manager, ui, selected, status, sizeof(status));
                        (void)build_inbox(app, manager);
                    } else activate_release(manager, ui, selected, status, sizeof(status));
                }
                redraw = true; break;
            case T5_UI_EVENT_TAP: {
                const int32_t hit = ui->hit_test(event.touch_x, event.touch_y);
                if (hit == T5_UI_HIT_HEADER) {
                    view = view == RELEASES ? SD_INBOX : RELEASES;
                    if (view == SD_INBOX) (void)build_inbox(app, manager);
                    else if (!refresh_releases(manager, ui))
                        copy_text(status, sizeof(status),
                                  "Package refresh failed; SD packages available");
                    else status[0] = '\0';
                    selected = 0; redraw = true;
                } else if (hit >= 0 && hit < (int32_t)row_count) {
                    if (selected == hit) {
                        if (view == SD_INBOX) {
                            activate_inbox(manager, ui, selected, status, sizeof(status));
                            (void)build_inbox(app, manager);
                        } else activate_release(manager, ui, selected, status, sizeof(status));
                    } else { selected = hit; status[0] = '\0'; }
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
