#include "T5AppApi.h"
#include "T5DriverManagerApi.h"
#include "T5PackageVersion.h"
#include "T5UiApi.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_DRIVER_ITEMS 64u
#define MAX_RECOVERY_ITEMS (MAX_DRIVER_ITEMS + 1u)
#define TITLE_MAX 80u
#define SUBTITLE_MAX (T5_DRIVER_CAPABILITY_MAX + T5_DRIVER_VERSION_MAX + 16u)
#define VALUE_MAX 40u
#define STATUS_MAX 160u

typedef enum {
    DRIVER_ACTION_INSTALL = 0,
    DRIVER_ACTION_UPDATE = 1,
    DRIVER_ACTION_CURRENT = 2,
    DRIVER_ACTION_INSTALLED_NEWER = 3,
    DRIVER_ACTION_UNAVAILABLE = 4,
} driver_action_t;

static t5_driver_catalog_entry_t entries[MAX_DRIVER_ITEMS];
static t5_ui_list_row_t rows[MAX_DRIVER_ITEMS];
static char row_titles[MAX_DRIVER_ITEMS][TITLE_MAX];
static char row_subtitles[MAX_DRIVER_ITEMS][SUBTITLE_MAX];
static char row_values[MAX_DRIVER_ITEMS][VALUE_MAX];
static uint32_t row_count;

// Static storage avoids overflowing the ELF application's small task stack.
static t5_driver_recovery_entry_t recovery_entries[MAX_RECOVERY_ITEMS];
static t5_ui_list_row_t recovery_rows[MAX_RECOVERY_ITEMS + 1u];
static char recovery_titles[MAX_RECOVERY_ITEMS][TITLE_MAX];
static char recovery_subtitles[MAX_RECOVERY_ITEMS][SUBTITLE_MAX];
static char recovery_values[MAX_RECOVERY_ITEMS][VALUE_MAX];

static bool has_driver_api(const t5_driver_manager_api_v1 *api) {
    const size_t required = offsetof(t5_driver_manager_api_v1, install) + sizeof(api->install);
    return api && api->api_version == T5_DRIVER_MANAGER_API_VERSION && api->struct_size >= required &&
           api->catalog_refresh && api->catalog_count && api->catalog_get &&
           api->installed_version_get && api->install;
}

static bool has_recovery_api(const t5_driver_manager_api_v1 *api) {
    const size_t required = offsetof(t5_driver_manager_api_v1, recovery_discard) + sizeof(api->recovery_discard);
    return has_driver_api(api) && api->struct_size >= required &&
           api->recovery_refresh && api->recovery_count && api->recovery_get &&
           api->recovery_retry && api->recovery_discard;
}

static bool has_ui_api(const t5_ui_api_v1 *ui) {
    const size_t required = offsetof(t5_ui_api_v1, previous_index) + sizeof(ui->previous_index);
    return ui && ui->struct_size >= required && ui->render_list && ui->hit_test && ui->poll_event &&
           ui->next_index && ui->previous_index;
}

static driver_action_t action_for(const t5_driver_manager_api_v1 *api, uint32_t index,
                                  char *installed, size_t installed_capacity) {
    if (installed && installed_capacity) installed[0] = '\0';
    if (index >= MAX_DRIVER_ITEMS || !installed || !installed_capacity) return DRIVER_ACTION_UNAVAILABLE;
    if (!api->installed_version_get(entries[index].id, installed, installed_capacity)) {
        return DRIVER_ACTION_INSTALL;
    }
    switch (t5_package_version_compare(entries[index].version, installed)) {
        case 1: return DRIVER_ACTION_UPDATE;
        case 0: return DRIVER_ACTION_CURRENT;
        case -1: return DRIVER_ACTION_INSTALLED_NEWER;
        default: return DRIVER_ACTION_UNAVAILABLE;
    }
}

static void build_rows(const t5_driver_manager_api_v1 *api) {
    uint32_t count = api->catalog_count();
    if (count > MAX_DRIVER_ITEMS) count = MAX_DRIVER_ITEMS;
    row_count = 0;
    for (uint32_t i = 0; i < count; ++i) {
        t5_driver_catalog_entry_t entry = {0};
        if (!api->catalog_get(i, &entry) || !entry.id[0] || !entry.version[0]) continue;
        entries[row_count] = entry;
        char installed[T5_DRIVER_VERSION_MAX] = {0};
        const driver_action_t action = action_for(api, row_count, installed, sizeof(installed));
        snprintf(row_titles[row_count], sizeof(row_titles[row_count]), "%s", entry.id);
        if (action == DRIVER_ACTION_CURRENT) {
            snprintf(row_subtitles[row_count], sizeof(row_subtitles[row_count]),
                     "%s - Installed", entry.capability[0] ? entry.capability : "Driver package");
        } else if (action == DRIVER_ACTION_INSTALLED_NEWER) {
            snprintf(row_subtitles[row_count], sizeof(row_subtitles[row_count]),
                     "Installed newer %s", installed);
        } else if (action == DRIVER_ACTION_UNAVAILABLE) {
            snprintf(row_subtitles[row_count], sizeof(row_subtitles[row_count]),
                     "Installed version invalid; inspect package");
        } else if (action == DRIVER_ACTION_UPDATE) {
            snprintf(row_subtitles[row_count], sizeof(row_subtitles[row_count]),
                     "%s - Installed %s", entry.capability[0] ? entry.capability : "Driver package", installed);
        } else {
            snprintf(row_subtitles[row_count], sizeof(row_subtitles[row_count]),
                     "%s - Not installed", entry.capability[0] ? entry.capability : "Driver package");
        }
        snprintf(row_values[row_count], sizeof(row_values[row_count]), "%s", entry.version);
        rows[row_count].title = row_titles[row_count];
        rows[row_count].subtitle = row_subtitles[row_count];
        rows[row_count].value = row_values[row_count];
        rows[row_count].flags = action == DRIVER_ACTION_UPDATE ? T5_UI_LIST_HIGHLIGHT_VALUE : 0;
        ++row_count;
    }
}

static const char *confirm_label(const t5_driver_manager_api_v1 *api, int32_t selected) {
    if (selected < 0 || selected >= (int32_t)row_count) return "";
    char installed[T5_DRIVER_VERSION_MAX] = {0};
    const driver_action_t action = action_for(api, (uint32_t)selected, installed, sizeof(installed));
    if (action == DRIVER_ACTION_UPDATE) return "Update";
    if (action == DRIVER_ACTION_INSTALL) return "Install";
    return "";
}

static void render_rows(const t5_driver_manager_api_v1 *api, const t5_ui_api_v1 *ui,
                        int32_t selected, const char *status) {
    const t5_ui_chrome_t chrome = {
        .title = "Driver Manager",
        .subtitle = "GitHub release drivers; header: recovery/refresh",
        .status = status ? status : "",
        .back_label = "Back",
        .confirm_label = confirm_label(api, selected),
        .previous_label = "Up",
        .next_label = "Down",
    };
    ui->render_list(&chrome, rows, row_count, selected);
}

static void render_loading(const t5_ui_api_v1 *ui, const char *status) {
    const t5_ui_list_row_t loading = {
        .title = "Loading driver catalog",
        .subtitle = "Using saved Wi-Fi",
        .value = "",
        .flags = 0,
    };
    const t5_ui_chrome_t chrome = {
        .title = "Driver Manager",
        .subtitle = "GitHub release drivers",
        .status = status ? status : "",
        .back_label = "Back",
        .confirm_label = "",
        .previous_label = "",
        .next_label = "",
    };
    ui->render_list(&chrome, &loading, 1, 0);
}

static bool refresh_catalog(const t5_driver_manager_api_v1 *api, const t5_ui_api_v1 *ui) {
    render_loading(ui, "Connecting with saved Wi-Fi...");
    if (!api->catalog_refresh()) return false;
    build_rows(api);
    return true;
}

static const char *recovery_state_text(uint8_t state) {
    switch (state) {
        case T5_DRIVER_RECOVERY_INCOMPLETE: return "Interrupted download - no manifest";
        case T5_DRIVER_RECOVERY_READY: return "Verified staged package - can retry";
        case T5_DRIVER_RECOVERY_INVALID: return "Incomplete/corrupt stage - inspect or discard";
        case T5_DRIVER_RECOVERY_STALE: return "Old/equal stage - cannot install";
        case T5_DRIVER_RECOVERY_MAPPED: return "Driver is mapped - stop it first";
        default: return "Unresolved generation - manual repair required";
    }
}

// Destructive deletion is never the default. A first Confirm on this page
// keeps the files; the user must select the second row and confirm again.
static bool confirm_discard(const t5_ui_api_v1 *ui, const t5_driver_recovery_entry_t *entry) {
    const t5_ui_list_row_t choices[2] = {
        {"Keep files", "Return without changing the SD card", "Cancel", 0},
        {"Discard retained files", "Cannot be undone; installed driver is untouched", "Discard", T5_UI_LIST_HIGHLIGHT_VALUE},
    };
    const t5_ui_chrome_t chrome = {
        .title = "Confirm discard",
        .subtitle = entry->kind == T5_DRIVER_RECOVERY_DOWNLOAD ? "Interrupted driver download" : entry->id,
        .status = "Select Discard, then Confirm; Back cancels",
        .back_label = "Cancel", .confirm_label = "Select", .previous_label = "Up", .next_label = "Down",
    };
    int32_t selected = 0;
    ui->render_list(&chrome, choices, 2, selected);
    for (;;) {
        t5_ui_event_t event = {0};
        if (!ui->poll_event(&event, 20)) return false;
        if (event.type == T5_UI_EVENT_BACK || event.type == T5_UI_EVENT_EXIT) return false;
        if (event.type == T5_UI_EVENT_PREVIOUS) selected = ui->previous_index(selected, 2);
        if (event.type == T5_UI_EVENT_NEXT) selected = ui->next_index(selected, 2);
        if (event.type == T5_UI_EVENT_TAP) {
            const int32_t hit = ui->hit_test(event.touch_x, event.touch_y);
            if (hit >= 0 && hit < 2) selected = hit;
            else continue;
        }
        if (event.type == T5_UI_EVENT_CONFIRM || event.type == T5_UI_EVENT_TAP) return selected == 1;
        ui->render_list(&chrome, choices, 2, selected);
    }
}

static void recovery_action_menu(const t5_driver_manager_api_v1 *api, const t5_ui_api_v1 *ui,
                                 uint32_t index, const t5_driver_recovery_entry_t *entry,
                                 char *status, size_t capacity) {
    t5_ui_list_row_t choices[3] = {
        {"Keep files", "Return to retained package list", "Cancel", 0}, {0}, {0},
    };
    uint32_t count = 1;
    int32_t retry_index = -1, discard_index = -1;
    if (entry->can_retry) {
        retry_index = (int32_t)count;
        choices[count++] = (t5_ui_list_row_t){"Retry verified stage", "Rehash and publish without downloading", "Retry", T5_UI_LIST_HIGHLIGHT_VALUE};
    }
    if (entry->can_discard) {
        discard_index = (int32_t)count;
        choices[count++] = (t5_ui_list_row_t){"Discard retained files", "Requires another explicit confirmation", "Discard", 0};
    }
    const t5_ui_chrome_t chrome = {
        .title = "Recovery actions",
        .subtitle = entry->kind == T5_DRIVER_RECOVERY_DOWNLOAD ? "Interrupted download" : entry->id,
        .status = recovery_state_text(entry->state),
        .back_label = "Cancel", .confirm_label = "Select", .previous_label = "Up", .next_label = "Down",
    };
    int32_t selected = 0;
    ui->render_list(&chrome, choices, count, selected);
    for (;;) {
        t5_ui_event_t event = {0};
        if (!ui->poll_event(&event, 20)) return;
        if (event.type == T5_UI_EVENT_BACK || event.type == T5_UI_EVENT_EXIT) return;
        if (event.type == T5_UI_EVENT_PREVIOUS) selected = ui->previous_index(selected, count);
        if (event.type == T5_UI_EVENT_NEXT) selected = ui->next_index(selected, count);
        if (event.type == T5_UI_EVENT_TAP) {
            const int32_t hit = ui->hit_test(event.touch_x, event.touch_y);
            if (hit >= 0 && hit < (int32_t)count) selected = hit;
            else continue;
        }
        if (event.type == T5_UI_EVENT_CONFIRM || event.type == T5_UI_EVENT_TAP) {
            if (selected == retry_index) {
                const bool okay = api->recovery_retry(index);
                snprintf(status, capacity, okay ? "Verified staged driver published" :
                         "Retry refused; retained stage was not discarded");
            } else if (selected == discard_index && confirm_discard(ui, entry)) {
                const bool okay = api->recovery_discard(index);
                snprintf(status, capacity, okay ? "Retained stage discarded; installed driver unchanged" :
                         "Discard refused; inspect retained files manually");
            }
            return;
        }
        ui->render_list(&chrome, choices, count, selected);
    }
}

// SD-only recovery runs BEFORE any Wi-Fi catalog request. It discovers stages
// even if their IDs no longer appear in the newest release catalog.
static bool show_recovery_screen(const t5_driver_manager_api_v1 *api, const t5_ui_api_v1 *ui) {
    if (!has_recovery_api(api) || !api->recovery_refresh()) return true;
    uint32_t count = api->recovery_count();
    if (count == 0) return true;
    char status[STATUS_MAX] = "Select a retained item; no file is deleted automatically";
    int32_t selected = 0;
    for (;;) {
        count = api->recovery_count();
        if (count > MAX_RECOVERY_ITEMS) count = MAX_RECOVERY_ITEMS;
        for (uint32_t i = 0; i < count; ++i) {
            recovery_entries[i] = (t5_driver_recovery_entry_t){0};
            const bool inspected = api->recovery_get(i, &recovery_entries[i]);
            const t5_driver_recovery_entry_t *item = &recovery_entries[i];
            snprintf(recovery_titles[i], sizeof(recovery_titles[i]), "%s",
                     inspected ? (item->kind == T5_DRIVER_RECOVERY_DOWNLOAD ?
                         "Interrupted driver download" : item->id) : "Inspection unavailable");
            snprintf(recovery_subtitles[i], sizeof(recovery_subtitles[i]), "%s",
                     inspected ? recovery_state_text(item->state) : "Cannot inspect; manual SD recovery required");
            snprintf(recovery_values[i], sizeof(recovery_values[i]), "%s",
                     inspected && item->candidate_version[0] ? item->candidate_version : "");
            recovery_rows[i] = (t5_ui_list_row_t){recovery_titles[i], recovery_subtitles[i],
                                                 recovery_values[i], item->can_retry ? T5_UI_LIST_HIGHLIGHT_VALUE : 0};
        }
        recovery_rows[count] = (t5_ui_list_row_t){"Continue to driver catalog", "Keep all remaining files", "Continue", 0};
        if (selected < 0 || selected > (int32_t)count) selected = (int32_t)count;
        const t5_ui_chrome_t chrome = {
            .title = "Driver recovery",
            .subtitle = "Offline SD inspection; Back opens catalog",
            .status = status,
            .back_label = "Catalog", .confirm_label = "Select", .previous_label = "Up", .next_label = "Down",
        };
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
                if (!api->recovery_refresh()) snprintf(status, sizeof(status), "Recovery inventory refresh failed");
                continue;
            }
            if (hit >= 0 && hit <= (int32_t)count) selected = hit;
            else continue;
        }
        if (event.type == T5_UI_EVENT_CONFIRM || event.type == T5_UI_EVENT_TAP) {
            if (selected == (int32_t)count) return true;
            t5_driver_recovery_entry_t entry = recovery_entries[selected];
            if (!entry.can_retry && !entry.can_discard) {
                snprintf(status, sizeof(status), "Recovery blocked; inspect SD or stop mapped driver");
            } else {
                recovery_action_menu(api, ui, (uint32_t)selected, &entry, status, sizeof(status));
            }
            if (!api->recovery_refresh()) snprintf(status, sizeof(status), "Recovery inventory refresh failed");
            selected = 0;
        }
    }
}

static bool wait_for_retry_or_exit(const t5_driver_manager_api_v1 *api, const t5_ui_api_v1 *ui) {
    const bool recovery = has_recovery_api(api) && api->recovery_refresh() && api->recovery_count() > 0;
    const t5_ui_list_row_t options[2] = {
        {"Retry driver refresh", "Unable to load release drivers", "Retry", T5_UI_LIST_HIGHLIGHT_VALUE},
        {"Inspect retained SD files", "Works offline; explicit discard only", "Recovery", 0},
    };
    const uint32_t count = recovery ? 2u : 1u;
    const t5_ui_chrome_t chrome = {
        .title = "Driver Manager",
        .subtitle = "GitHub release drivers",
        .status = "Check saved Wi-Fi, or inspect recovery files",
        .back_label = "Back", .confirm_label = "Select", .previous_label = "Up", .next_label = "Down",
    };
    int32_t selected = 0;
    ui->render_list(&chrome, options, count, selected);
    for (;;) {
        t5_ui_event_t event = {0};
        if (!ui->poll_event(&event, 20)) return false;
        if (event.type == T5_UI_EVENT_BACK || event.type == T5_UI_EVENT_EXIT) return false;
        if (event.type == T5_UI_EVENT_PREVIOUS) selected = ui->previous_index(selected, count);
        if (event.type == T5_UI_EVENT_NEXT) selected = ui->next_index(selected, count);
        if (event.type == T5_UI_EVENT_TAP) {
            const int32_t hit = ui->hit_test(event.touch_x, event.touch_y);
            if (hit >= 0 && hit < (int32_t)count) selected = hit;
            else continue;
        }
        if (event.type == T5_UI_EVENT_CONFIRM || event.type == T5_UI_EVENT_TAP) {
            if (selected == 1) return show_recovery_screen(api, ui);
            return true;
        }
        ui->render_list(&chrome, options, count, selected);
    }
}

static void activate_selected(const t5_driver_manager_api_v1 *api, const t5_ui_api_v1 *ui,
                              int32_t selected, char *status, size_t status_capacity) {
    if (!status || !status_capacity || selected < 0 || selected >= (int32_t)row_count) return;
    char installed[T5_DRIVER_VERSION_MAX] = {0};
    const driver_action_t action = action_for(api, (uint32_t)selected, installed, sizeof(installed));
    if (action == DRIVER_ACTION_CURRENT) {
        snprintf(status, status_capacity, "%s is already current", entries[selected].id);
        return;
    }
    if (action == DRIVER_ACTION_INSTALLED_NEWER) {
        snprintf(status, status_capacity, "%s: installed %s is newer than catalog", entries[selected].id, installed);
        return;
    }
    if (action == DRIVER_ACTION_UNAVAILABLE) {
        snprintf(status, status_capacity, "%s: version invalid; inspect installation", entries[selected].id);
        return;
    }
    snprintf(status, status_capacity, "%s %s...",
             action == DRIVER_ACTION_UPDATE ? "Updating" : "Installing", entries[selected].id);
    render_rows(api, ui, selected, status);
    const bool ok = api->install((uint32_t)selected);
    build_rows(api);
    if (ok) {
        snprintf(status, status_capacity, "%s %s; activation unchanged", entries[selected].id,
                 action == DRIVER_ACTION_UPDATE ? "updated" : "installed");
    } else {
        snprintf(status, status_capacity, "%s %s failed; header: recovery", entries[selected].id,
                 action == DRIVER_ACTION_UPDATE ? "update" : "install");
    }
}

__attribute__((visibility("default"))) void app_main(void) {
    const t5_app_api_v1 *app = t5_app_get_api(T5_APP_ABI_VERSION);
    const t5_driver_manager_api_v1 *drivers = t5_driver_manager_get_api(T5_DRIVER_MANAGER_API_VERSION);
    const t5_ui_api_v1 *ui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!app || !app->set_back_exits_app || !has_driver_api(drivers) || !has_ui_api(ui)) return;
    app->set_back_exits_app(false);
    if (has_recovery_api(drivers) && !show_recovery_screen(drivers, ui)) {
        app->set_back_exits_app(true);
        return;
    }
    bool loaded = refresh_catalog(drivers, ui);
    while (!loaded) {
        if (!wait_for_retry_or_exit(drivers, ui)) {
            app->set_back_exits_app(true);
            return;
        }
        loaded = refresh_catalog(drivers, ui);
    }
    int32_t selected = 0;
    char status[STATUS_MAX] = {0};
    if (row_count == 0) snprintf(status, sizeof(status), "No driver packages found in latest release");
    render_rows(drivers, ui, selected, status);
    for (;;) {
        t5_ui_event_t event = {0};
        if (!ui->poll_event(&event, 20)) break;
        bool redraw = false;
        switch (event.type) {
            case T5_UI_EVENT_PREVIOUS:
                selected = ui->previous_index(selected, row_count);
                status[0] = '\0';
                redraw = true;
                break;
            case T5_UI_EVENT_NEXT:
                selected = ui->next_index(selected, row_count);
                status[0] = '\0';
                redraw = true;
                break;
            case T5_UI_EVENT_CONFIRM:
                activate_selected(drivers, ui, selected, status, sizeof(status));
                redraw = true;
                break;
            case T5_UI_EVENT_TAP: {
                const int32_t hit = ui->hit_test(event.touch_x, event.touch_y);
                if (hit >= 0 && hit < (int32_t)row_count) {
                    if (hit == selected) activate_selected(drivers, ui, selected, status, sizeof(status));
                    else {
                        selected = hit;
                        status[0] = '\0';
                    }
                    redraw = true;
                } else if (hit == T5_UI_HIT_HEADER) {
                    if (has_recovery_api(drivers) && drivers->recovery_refresh() && drivers->recovery_count() > 0) {
                        if (!show_recovery_screen(drivers, ui)) {
                            app->set_back_exits_app(true);
                            return;
                        }
                    } else {
                        loaded = refresh_catalog(drivers, ui);
                        while (!loaded) {
                            if (!wait_for_retry_or_exit(drivers, ui)) {
                                app->set_back_exits_app(true);
                                return;
                            }
                            loaded = refresh_catalog(drivers, ui);
                        }
                    }
                    selected = 0;
                    status[0] = '\0';
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
        if (row_count == 0) selected = 0;
        else if (selected >= (int32_t)row_count) selected = (int32_t)row_count - 1;
        if (redraw) render_rows(drivers, ui, selected, status);
    }
    app->set_back_exits_app(true);
}
