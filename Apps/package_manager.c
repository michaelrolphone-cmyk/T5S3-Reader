#include "T5AppApi.h"
#include "T5PackageApi.h"
#include "T5UiApi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_PACKAGES 64u
#define NAME_BYTES 128u
#define STATUS_BYTES 160u
#define INBOX_VFS "/sd/Packages/inbox"

static char filenames[MAX_PACKAGES][NAME_BYTES];
static t5_ui_list_row_t rows[MAX_PACKAGES];
static uint32_t package_count;

static bool package_filename(const char *name) {
    if (!name) return false;
    const size_t size = strlen(name);
    if (size < 6 || size >= NAME_BYTES || strcmp(name + size - 5, ".risc")) return false;
    for (size_t i = 0; i < size; ++i) {
        const char ch = name[i];
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) continue;
        if (i > 0 && i + 1 < size && (ch == '-' || ch == '_' || ch == '.')) {
            if (ch == '.' && name[i - 1] == '.') return false;
            continue;
        }
        return false;
    }
    return true;
}

static bool scan_inbox(const t5_app_api_v1 *app) {
    package_count = 0;
    if (!app->dir_open(INBOX_VFS)) return false;
    t5_app_dirent_t entry = {0};
    while (package_count < MAX_PACKAGES && app->dir_next(&entry)) {
        if (entry.is_directory || !package_filename(entry.name)) continue;
        snprintf(filenames[package_count], NAME_BYTES, "%s", entry.name);
        rows[package_count].title = filenames[package_count];
        rows[package_count].subtitle = "P-256 signature checked before install";
        rows[package_count].value = "Inspect";
        rows[package_count].flags = 0;
        ++package_count;
    }
    app->dir_close();
    return true;
}

static void render(const t5_ui_api_v1 *ui, bool enrolled,
                   bool folder_exists, int32_t selected, const char *status) {
    const t5_ui_list_row_t placeholder = {
        .title = enrolled ? (folder_exists ? "Inbox empty" : "Create /Packages/inbox") :
                            "No firmware signer configured",
        .subtitle = enrolled ? "Place signed .risc files on the SD card" :
                               "Signed installation is disabled safely",
        .value = "",
        .flags = 0,
    };
    const t5_ui_chrome_t chrome = {
        .title = "Package Manager (Dev)",
        .subtitle = "Signed offline install - inactive only",
        .status = status ? status : "",
        .back_label = "Back",
        .confirm_label = enrolled && package_count ? "Inspect" : "",
        .previous_label = package_count > 1 ? "Up" : "",
        .next_label = package_count > 1 ? "Down" : "",
    };
    ui->render_list(&chrome, package_count ? rows : &placeholder,
                    package_count ? package_count : 1, selected);
}

__attribute__((visibility("default"))) void app_main(void) {
    const t5_app_api_v1 *app = t5_app_get_api(T5_APP_ABI_VERSION);
    const t5_ui_api_v1 *ui = t5_ui_get_api(T5_UI_API_VERSION);
    const t5_package_api_v1 *packages = t5_package_get_api(T5_PACKAGE_API_VERSION);
    if (!app || !ui || !packages ||
        app->struct_size < offsetof(t5_app_api_v1, set_back_exits_app) +
                            sizeof(app->set_back_exits_app) ||
        ui->struct_size < offsetof(t5_ui_api_v1, previous_index) +
                          sizeof(ui->previous_index) ||
        packages->struct_size < offsetof(t5_package_api_v1, install_take_result) +
                                sizeof(packages->install_take_result) ||
        !app->dir_open || !app->dir_next || !app->dir_close ||
        !app->set_back_exits_app || !ui->render_list || !ui->poll_event ||
        !ui->hit_test || !ui->next_index || !ui->previous_index ||
        !packages->available || !packages->install_request ||
        !packages->install_take_result) return;

    app->set_back_exits_app(false);
    char status[STATUS_BYTES] = {0};
    t5_package_result_t previous = T5_PACKAGE_RESULT_FAILED;
    uint64_t previous_cookie = 0;
    if (packages->install_take_result(&previous, &previous_cookie)) {
        (void)previous_cookie;
        switch (previous) {
            case T5_PACKAGE_RESULT_INSTALLED_INACTIVE:
                snprintf(status, sizeof(status), "Authenticated package installed INACTIVE"); break;
            case T5_PACKAGE_RESULT_CANCELLED:
                snprintf(status, sizeof(status), "Install cancelled"); break;
            case T5_PACKAGE_RESULT_UNTRUSTED:
                snprintf(status, sizeof(status), "Signer, content or selected package changed"); break;
            case T5_PACKAGE_RESULT_STALE_STAGE:
                snprintf(status, sizeof(status), "Foreign/incomplete stage preserved for recovery"); break;
            case T5_PACKAGE_RESULT_PENDING_RECOVERY:
                snprintf(status, sizeof(status), "Install pending explicit recovery"); break;
            default:
                snprintf(status, sizeof(status), "Install failed; see serial log"); break;
        }
    }

    bool enrolled = packages->available();
    bool folder_exists = enrolled && scan_inbox(app);
    int32_t selected = 0;
    render(ui, enrolled, folder_exists, selected, status);
    for (;;) {
        t5_ui_event_t event = {0};
        if (!ui->poll_event(&event, 20)) break;
        bool redraw = false;
        if (event.type == T5_UI_EVENT_BACK || event.type == T5_UI_EVENT_EXIT) break;
        if (event.type == T5_UI_EVENT_PREVIOUS && package_count) {
            selected = ui->previous_index(selected, package_count);
            redraw = true;
        } else if (event.type == T5_UI_EVENT_NEXT && package_count) {
            selected = ui->next_index(selected, package_count);
            redraw = true;
        } else if (event.type == T5_UI_EVENT_CONFIRM || event.type == T5_UI_EVENT_TAP) {
            const int32_t hit = event.type == T5_UI_EVENT_TAP ?
                ui->hit_test(event.touch_x, event.touch_y) : selected;
            if (hit == T5_UI_HIT_HEADER) {
                enrolled = packages->available();
                folder_exists = enrolled && scan_inbox(app);
                if (selected >= (int32_t)package_count) selected = 0;
                redraw = true;
            } else if (enrolled && package_count && hit >= 0 &&
                       hit < (int32_t)package_count) {
                selected = hit;
                char path[sizeof(INBOX_VFS) + NAME_BYTES + 2u];
                const int written = snprintf(path, sizeof(path), "%s/%s",
                                             INBOX_VFS, filenames[selected]);
                if (written > 0 && (size_t)written < sizeof(path) &&
                    packages->install_request(path, (uint64_t)selected + 1u)) {
                    // The firmware-owned confirmation activity now owns the
                    // operation. Yield the app; never self-authorize installs.
                    app->set_back_exits_app(true);
                    return;
                }
                snprintf(status, sizeof(status), "Signed inspection rejected; check firmware signer");
                redraw = true;
            }
        }
        if (redraw) render(ui, enrolled, folder_exists, selected, status);
    }
    app->set_back_exits_app(true);
}
