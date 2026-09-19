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

// Native ELF symbols are bounded: avoid importing unexported libc helpers.
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
static bool catalog_available(const t5_app_api_v1 *app) {
    return app && app->struct_size >= offsetof(t5_app_api_v1, app_catalog_download) +
        sizeof(app->app_catalog_download) && app->app_catalog_refresh &&
        app->app_catalog_count && app->app_catalog_get && app->app_catalog_download;
}
static bool package_available(const t5_package_manager_api_v1 *manager) {
    return manager && manager->api_version == T5_PACKAGE_MANAGER_API_VERSION &&
        manager->struct_size >= offsetof(t5_package_manager_api_v1, preview_archive) &&
        manager->preview && manager->install && manager->uninstall;
}
static bool zip_available(const t5_package_manager_api_v1 *manager) {
    return manager->struct_size >= offsetof(t5_package_manager_api_v1, install_archive) +
        sizeof(manager->install_archive) && manager->preview_archive &&
        manager->install_archive;
}
static bool ui_available(const t5_ui_api_v1 *ui) {
    return ui && ui->struct_size >= offsetof(t5_ui_api_v1, previous_index) +
        sizeof(ui->previous_index) && ui->render_list && ui->hit_test &&
        ui->poll_event && ui->next_index && ui->previous_index;
}
static bool release_manifest(const t5_app_api_v1 *app, uint32_t index,
                             t5_app_manifest_t *out) {
    if (!out || app->struct_size < offsetof(t5_app_api_v1, app_catalog_manifest_get) +
        sizeof(app->app_catalog_manifest_get) || !app->app_catalog_manifest_get) return false;
    *out = (t5_app_manifest_t){0};
    return app->app_catalog_manifest_get(index, out);
}
static bool version_available(const t5_app_api_v1 *app) {
    return app->struct_size >= offsetof(t5_app_api_v1, app_catalog_version_get) +
        sizeof(app->app_catalog_version_get) && app->app_catalog_version_get &&
        app->installed_app_version_get;
}
static void row(uint32_t index, const char *title, const char *subtitle,
                const char *value, bool highlighted) {
    copy_text(titles[index], sizeof(titles[index]), title);
    copy_text(subtitles[index], sizeof(subtitles[index]), subtitle);
    copy_text(values[index], sizeof(values[index]), value);
    rows[index] = (t5_ui_list_row_t){titles[index], subtitles[index], values[index],
                                    highlighted ? T5_UI_LIST_HIGHLIGHT_VALUE : 0};
}
static bool release_versions(const t5_app_api_v1 *app, uint32_t index,
                             const t5_app_manifest_t *manifest,
                             char *available, char *installed) {
    available[0] = installed[0] = '\0';
    if (!manifest || !version_available(app) ||
        !app->app_catalog_version_get(index, available, T5_APP_VERSION_MAX)) return false;
    return app->installed_app_version_get(manifest->file_name, installed,
                                         T5_APP_VERSION_MAX);
}
static void build_releases(const t5_app_api_v1 *app) {
    row_count = 0;
    const uint32_t count = app->app_catalog_count();
    for (uint32_t index = 0; index < count && row_count < MAX_ROWS; ++index) {
        t5_app_release_asset_t asset = {0};
        t5_app_manifest_t manifest = {0};
        if (!app->app_catalog_get(index, &asset)) continue;
        const bool has_manifest = release_manifest(app, index, &manifest);
        char latest[T5_APP_VERSION_MAX] = {0};
        char installed[T5_APP_VERSION_MAX] = {0};
        const bool installed_now = has_manifest &&
            release_versions(app, index, &manifest, latest, installed);
        const bool current = installed_now && strcmp(latest, installed) == 0;
        const char *subtitle = has_manifest && !manifest.compatible ?
            "Requires newer firmware" : current ? "Installed" : installed_now ?
            "Update available" : "Not installed";
        row(row_count, has_manifest ? manifest.display_name : asset.name,
            subtitle, latest[0] ? latest : current ? "Current" :
            installed_now ? "Update" : "Install", installed_now && !current);
        release_indices[row_count++] = index;
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
        const bool archive = !entry.is_directory && zip_name(entry.name) &&
                             zip_available(manager);
        if (!entry.is_directory && !archive) continue;
        t5_package_preview_t info = {0};
        const bool described = archive ? manager->preview_archive(entry.name, &info) :
                                        manager->preview(entry.name, &info);
        if (!described || info.kind != T5_PACKAGE_APPLICATION) continue;
        memcpy(folders[row_count], entry.name, length + 1u);
        archive_rows[row_count] = archive;
        packages[row_count] = info;
        char detail[SUBTITLE_SIZE] = {0};
        if (!info.valid_installation)
            copy_text(detail, sizeof(detail), "Installed package needs recovery");
        else if (info.installed_version[0]) {
            snprintf(detail, sizeof(detail), "Installed %.31s; %s", info.installed_version,
                     info.install_allowed ? "update available" : "no update");
        } else copy_text(detail, sizeof(detail), info.install_allowed ?
            archive ? "SD ZIP: ready to install" : "SD directory: ready to install" :
            "Dependency, version or stage blocked");
        row(row_count, info.id, detail, info.version, info.install_allowed != 0);
        ++row_count;
    }
    app->dir_close();
    return true;
}
static bool refresh_releases(const t5_app_api_v1 *app, const t5_ui_api_v1 *ui) {
    const t5_ui_list_row_t loading = {"Loading release catalog", "Using saved Wi-Fi", "", 0};
    const t5_ui_chrome_t chrome = {"App Store", "Release catalog", "Connecting...",
                                   "Back", "", "", ""};
    ui->render_list(&chrome, &loading, 1, 0);
    if (!app->app_catalog_refresh()) return false;
    build_releases(app);
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
static const char *action_label(const t5_app_api_v1 *app, int32_t selected) {
    if (selected < 0 || selected >= (int32_t)row_count) return "";
    if (view == SD_INBOX) {
        const t5_package_preview_t *package = &packages[selected];
        return !package->valid_installation ? "" : package->install_allowed ?
            package->installed_version[0] ? "Update" : "Install" :
            package->installed_version[0] ? "Actions" : "";
    }
    t5_app_manifest_t manifest = {0};
    const uint32_t index = release_indices[selected];
    if (release_manifest(app, index, &manifest)) {
        if (!manifest.compatible) return "";
        char latest[T5_APP_VERSION_MAX] = {0}, installed[T5_APP_VERSION_MAX] = {0};
        if (release_versions(app, index, &manifest, latest, installed)) {
            if (!strcmp(latest, installed)) return "";
            return "Update";
        }
    }
    return "Install";
}
static void render(const t5_app_api_v1 *app, const t5_ui_api_v1 *ui,
                   int32_t selected, const char *status) {
    const t5_ui_chrome_t chrome = {
        .title = "App Store",
        .subtitle = view == SD_INBOX ? "SD ZIP/packages; tap header for releases" :
                    "Latest release; tap header for SD packages",
        .status = status,
        .back_label = "Back", .confirm_label = action_label(app, selected),
        .previous_label = "Up", .next_label = "Down",
    };
    if (row_count) ui->render_list(&chrome, rows, row_count, selected);
    else {
        const t5_ui_list_row_t empty = {"No applications", view == SD_INBOX ?
            "Add .rte.zip or package under /Packages/Inbox" :
            "Release catalog unavailable", "", 0};
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
static void activate_release(const t5_app_api_v1 *app, const t5_ui_api_v1 *ui,
                             int32_t selected, char *status, size_t capacity) {
    const uint32_t index = release_indices[selected];
    t5_app_release_asset_t asset = {0};
    t5_app_manifest_t manifest = {0};
    if (!app->app_catalog_get(index, &asset)) return;
    const bool known = release_manifest(app, index, &manifest);
    const char *name = known ? manifest.display_name : asset.name;
    char latest[T5_APP_VERSION_MAX] = {0}, installed[T5_APP_VERSION_MAX] = {0};
    const bool installed_now = known && release_versions(app, index, &manifest, latest, installed);
    if (known && !manifest.compatible) {
        snprintf(status, capacity, "%.95s requires newer firmware", name);
        return;
    }
    if (installed_now && !strcmp(latest, installed)) {
        snprintf(status, capacity, "%.95s already current", name);
        return;
    }
    snprintf(status, capacity, "%s %.95s...", installed_now ? "Updating" : "Installing", name);
    render(app, ui, selected, status);
    const bool okay = app->app_catalog_download(index);
    build_releases(app);
    snprintf(status, capacity, "%.95s: %s", name, okay ? "installed" : "installation failed");
}

__attribute__((visibility("default"))) void app_main(void) {
    const t5_app_api_v1 *app = t5_app_get_api(T5_APP_ABI_VERSION);
    const t5_package_manager_api_v1 *manager = t5_package_manager_get_api(T5_PACKAGE_MANAGER_API_VERSION);
    const t5_ui_api_v1 *ui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!catalog_available(app) || !package_available(manager) || !ui_available(ui) ||
        !app->set_back_exits_app || !app->dir_open || !app->dir_next || !app->dir_close) return;
    app->set_back_exits_app(false);
    view = RELEASES;
    if (!refresh_releases(app, ui)) {
        view = SD_INBOX;
        (void)build_inbox(app, manager);
    }
    int32_t selected = 0;
    char status[STATUS_SIZE] = {0};
    render(app, ui, selected, status);
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
                    } else activate_release(app, ui, selected, status, sizeof(status));
                }
                redraw = true; break;
            case T5_UI_EVENT_TAP: {
                const int32_t hit = ui->hit_test(event.touch_x, event.touch_y);
                if (hit == T5_UI_HIT_HEADER) {
                    view = view == RELEASES ? SD_INBOX : RELEASES;
                    if (view == SD_INBOX) (void)build_inbox(app, manager);
                    else if (!refresh_releases(app, ui))
                        copy_text(status, sizeof(status), "Release refresh failed; SD packages available");
                    else status[0] = '\0';
                    selected = 0; redraw = true;
                } else if (hit >= 0 && hit < (int32_t)row_count) {
                    if (selected == hit) {
                        if (view == SD_INBOX) {
                            activate_inbox(manager, ui, selected, status, sizeof(status));
                            (void)build_inbox(app, manager);
                        } else activate_release(app, ui, selected, status, sizeof(status));
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
        if (redraw) render(app, ui, selected, status);
    }
    app->set_back_exits_app(true);
}
