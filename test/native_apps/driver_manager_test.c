#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../Apps/driver_manager.c"

static unsigned online_installs, directory_installs, zip_installs, removals;
static unsigned legacy_refreshes, online_refreshes, recovery_refreshes;
static unsigned retries, discards, rendered, back_disabled, back_restored;
static uint32_t recovery_items;
static t5_ui_event_t events[24];
static unsigned queued, consumed, directory_cursor;

static void queue(uint8_t type) {
    assert(queued < sizeof(events) / sizeof(events[0]));
    events[queued++] = (t5_ui_event_t){.type = type};
}
static void reset_queue(void) { queued = consumed = 0; }
static bool poll(t5_ui_event_t *out, uint32_t delay_ms) {
    (void)delay_ms;
    if (consumed >= queued) return false;
    *out = events[consumed++];
    return true;
}
static int32_t next(int32_t selected, uint32_t count) {
    return count ? (selected + 1) % (int32_t)count : 0;
}
static int32_t previous(int32_t selected, uint32_t count) {
    return count ? (selected + (int32_t)count - 1) % (int32_t)count : 0;
}
static int32_t hit(int16_t x, int16_t y) { (void)y; return x; }
static void draw(const t5_ui_chrome_t *chrome, const t5_ui_list_row_t *items,
                 uint32_t count, int32_t selected) {
    assert(chrome && items && count && selected >= 0 && selected < (int32_t)count);
    ++rendered;
}
static void set_back(bool enabled) {
    if (enabled) ++back_restored;
    else ++back_disabled;
}
static bool dir_open(const char *path) {
    assert(!strcmp(path, "/sd/Packages/Inbox"));
    directory_cursor = 0;
    return true;
}
static bool dir_next(t5_app_dirent_t *out) {
    assert(out);
    *out = (t5_app_dirent_t){0};
    switch (directory_cursor++) {
        case 0:
            strcpy(out->name, "driver-gps-nmea-1.0.5-xtensa-esp32s3.rte.zip");
            return true;
        case 1:
            strcpy(out->name, "gps-legacy");
            out->is_directory = 1;
            return true;
        default: return false;
    }
}
static void dir_close(void) {}
static const t5_app_api_v1 app_api = {
    .abi_version = T5_APP_ABI_VERSION,
    .struct_size = sizeof(t5_app_api_v1),
    .dir_open = dir_open, .dir_next = dir_next, .dir_close = dir_close,
    .set_back_exits_app = set_back,
};
static void make_preview(t5_package_preview_t *p, const char *id) {
    *p = (t5_package_preview_t){0};
    p->kind = T5_PACKAGE_DRIVER;
    p->valid_installation = p->install_allowed = 1;
    snprintf(p->id, sizeof(p->id), "%s", id);
    strcpy(p->version, "1.0.5");
}
static bool online_refresh(void) { ++online_refreshes; return true; }
static uint32_t online_count(void) { return 2; }
static bool online_get(uint32_t index, t5_package_catalog_row_t *row) {
    if (!row || index > 1) return false;
    *row = (t5_package_catalog_row_t){0};
    make_preview(&row->package, index ? "gps-nmea" : "board-power-t5s3-v2");
    snprintf(row->archive, sizeof(row->archive), "driver-%s-1.0.5-xtensa-esp32s3.rte.zip",
             row->package.id);
    return true;
}
static bool online_install(uint32_t index) {
    assert(index < 2);
    ++online_installs;
    return true;
}
static bool preview_directory(const char *folder, t5_package_preview_t *p) {
    assert(!strcmp(folder, "gps-legacy"));
    make_preview(p, "gps-legacy");
    return true;
}
static bool preview_archive(const char *archive, t5_package_preview_t *p) {
    assert(!strcmp(archive, "driver-gps-nmea-1.0.5-xtensa-esp32s3.rte.zip"));
    make_preview(p, "gps-nmea");
    return true;
}
static bool install_directory(const char *folder) {
    assert(!strcmp(folder, "gps-legacy"));
    ++directory_installs;
    return true;
}
static bool install_archive(const char *name) {
    assert(!strcmp(name, "driver-gps-nmea-1.0.5-xtensa-esp32s3.rte.zip"));
    ++zip_installs;
    return true;
}
static bool uninstall(uint8_t kind, const char *id) {
    assert(kind == T5_PACKAGE_DRIVER && !strcmp(id, "gps-nmea"));
    ++removals;
    return true;
}
static const t5_package_manager_api_v1 package_api = {
    .api_version = T5_PACKAGE_MANAGER_API_VERSION,
    .struct_size = sizeof(t5_package_manager_api_v1),
    .preview = preview_directory, .install = install_directory, .uninstall = uninstall,
    .preview_archive = preview_archive, .install_archive = install_archive,
    .online_refresh = online_refresh, .online_count = online_count,
    .online_get = online_get, .online_install = online_install,
};
static bool legacy_refresh(void) { ++legacy_refreshes; return true; }
static bool recovery_refresh(void) { ++recovery_refreshes; return true; }
static uint32_t recovery_count(void) { return recovery_items; }
static bool recovery_get(uint32_t index, t5_driver_recovery_entry_t *out) {
    if (!out || index || !recovery_items) return false;
    *out = (t5_driver_recovery_entry_t){0};
    out->kind = T5_DRIVER_RECOVERY_STAGE;
    out->state = T5_DRIVER_RECOVERY_READY;
    out->can_retry = out->can_discard = true;
    strcpy(out->id, "gps-nmea");
    strcpy(out->candidate_version, "1.0.5");
    return true;
}
static bool retry(uint32_t index) {
    assert(index == 0 && recovery_items == 1);
    ++retries;
    recovery_items = 0;
    return true;
}
static bool discard(uint32_t index) {
    assert(index == 0 && recovery_items == 1);
    ++discards;
    recovery_items = 0;
    return true;
}
static const t5_driver_manager_api_v1 driver_api = {
    .api_version = T5_DRIVER_MANAGER_API_VERSION,
    .struct_size = sizeof(t5_driver_manager_api_v1),
    .catalog_refresh = legacy_refresh,
    .recovery_refresh = recovery_refresh, .recovery_count = recovery_count,
    .recovery_get = recovery_get, .recovery_retry = retry,
    .recovery_discard = discard,
};
static const t5_ui_api_v1 ui_api = {
    .api_version = T5_UI_API_VERSION, .struct_size = sizeof(t5_ui_api_v1),
    .render_list = draw, .poll_event = poll, .hit_test = hit,
    .next_index = next, .previous_index = previous,
};
const t5_app_api_v1 *t5_app_get_api(uint32_t v) {
    assert(v == T5_APP_ABI_VERSION); return &app_api;
}
const t5_driver_manager_api_v1 *t5_driver_manager_get_api(uint32_t v) {
    assert(v == T5_DRIVER_MANAGER_API_VERSION); return &driver_api;
}
const t5_package_manager_api_v1 *t5_package_manager_get_api(uint32_t v) {
    assert(v == T5_PACKAGE_MANAGER_API_VERSION); return &package_api;
}
const t5_ui_api_v1 *t5_ui_get_api(uint32_t v) {
    assert(v == T5_UI_API_VERSION); return &ui_api;
}
int main(void) {
    assert(app_ready(&app_api) && ui_ready(&ui_api) && manager_ready(&package_api));
    assert(recovery_ready(&driver_api));
    assert(refresh_online(&package_api, &ui_api));
    assert(row_count == 2 && source == ONLINE);
    assert(!strcmp(online[1].package.id, "gps-nmea"));
    char status[STATUS_BYTES] = {0};
    activate(&package_api, &ui_api, 1, status, sizeof(status));
    assert(online_installs == 1 && !legacy_refreshes && strstr(status, "installed"));
    populate_online(&package_api);
    assert(online_refreshes == 1); // No drifting refresh after installation.
    source = INBOX;
    assert(load_inbox(&app_api, &package_api) && row_count == 2);
    assert(offline_zip[0] && !offline_zip[1]);
    activate(&package_api, &ui_api, 0, status, sizeof(status));
    activate(&package_api, &ui_api, 1, status, sizeof(status));
    assert(zip_installs == 1 && directory_installs == 1);
    recovery_items = 1;
    reset_queue(); queue(T5_UI_EVENT_CONFIRM); queue(T5_UI_EVENT_CONFIRM);
    queue(T5_UI_EVENT_BACK);
    assert(recovery_screen(&driver_api, &ui_api));
    assert(retries == 0 && discards == 0); // Default recovery action cancels.
    reset_queue(); queue(T5_UI_EVENT_CONFIRM);
    queue(T5_UI_EVENT_NEXT); queue(T5_UI_EVENT_CONFIRM);
    assert(recovery_screen(&driver_api, &ui_api));
    assert(retries == 1 && recovery_items == 0);
    recovery_items = 1;
    reset_queue(); queue(T5_UI_EVENT_CONFIRM);
    queue(T5_UI_EVENT_NEXT); queue(T5_UI_EVENT_NEXT); queue(T5_UI_EVENT_CONFIRM);
    queue(T5_UI_EVENT_NEXT); queue(T5_UI_EVENT_CONFIRM);
    assert(recovery_screen(&driver_api, &ui_api));
    assert(discards == 1 && recovery_items == 0);
    reset_queue(); queue(T5_UI_EVENT_EXIT);
    app_main();
    assert(back_disabled && back_restored);
    assert(!legacy_refreshes && rendered && recovery_refreshes >= 3);
    puts("Driver Manager: generic online/SD ZIP and explicit recovery contract PASS");
    return 0;
}
