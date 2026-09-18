#include <assert.h>
#include <stdio.h>
#include <string.h>

// Compile the actual app and exercise its static UI and action paths.
#include "../../Apps/driver_manager.c"

static const char *catalog_version;
static const char *installed_version;
static unsigned install_calls;
static unsigned rendered;
static unsigned recovery_rendered;
static unsigned recovery_discards;
static unsigned recovery_retries;
static unsigned recovery_refreshes;
static uint32_t retained_count;
static uint8_t retained_kind;
static uint8_t retained_state;
static bool retained_retry;
static bool retained_discard;
static t5_ui_event_t queued[24];
static size_t queued_count;
static size_t queued_next;
static unsigned progress_rendered;
static char progress_current[64];
static char progress_phase[96];
static char progress_value[64];
static char progress_file[64];
static char progress_previous[128];
static char progress_status[STATUS_BYTES];

static void enqueue(uint8_t type) {
    assert(queued_count < sizeof(queued) / sizeof(queued[0]));
    queued[queued_count++] = (t5_ui_event_t){.type = type};
}
static void reset_queue(void) { queued_count = queued_next = 0; }
static bool mock_poll(t5_ui_event_t *event, uint32_t wait_ms) {
    (void)wait_ms;
    if (queued_next == queued_count) return false;
    *event = queued[queued_next++];
    return true;
}
static int32_t mock_next(int32_t index, uint32_t count) {
    return count ? (index + 1) % (int32_t)count : 0;
}
static int32_t mock_previous(int32_t index, uint32_t count) {
    return count ? (index + (int32_t)count - 1) % (int32_t)count : 0;
}
static int32_t mock_hit(int16_t x, int16_t y) {
    (void)y;
    return x;
}
static bool mock_recovery_refresh(void) { ++recovery_refreshes; return true; }
static uint32_t mock_recovery_count(void) { return retained_count; }
static bool mock_recovery_get(uint32_t index, t5_driver_recovery_entry_t *out) {
    if (index || !retained_count || !out) return false;
    *out = (t5_driver_recovery_entry_t){0};
    out->kind = retained_kind;
    out->state = retained_state;
    out->can_retry = retained_retry;
    out->can_discard = retained_discard;
    out->size_bytes = 17;
    if (retained_kind == T5_DRIVER_RECOVERY_STAGE) {
        snprintf(out->id, sizeof(out->id), "gps-nmea");
        snprintf(out->candidate_version, sizeof(out->candidate_version), "1.0.1");
    }
    return true;
}
static bool mock_recovery_retry(uint32_t index) {
    assert(index == 0 && retained_count && retained_retry);
    ++recovery_retries;
    retained_count = 0;
    return true;
}
static bool mock_recovery_discard(uint32_t index) {
    assert(index == 0 && retained_count && retained_discard);
    ++recovery_discards;
    retained_count = 0;
    return true;
}
static bool mock_catalog_refresh(void) { return true; }
static uint32_t mock_count(void) { return 1; }
static bool mock_get(uint32_t index, t5_driver_catalog_entry_t *out) {
    if (index || !out) return false;
    *out = (t5_driver_catalog_entry_t){0};
    snprintf(out->id, sizeof(out->id), "gps-nmea");
    snprintf(out->version, sizeof(out->version), "%s", catalog_version);
    snprintf(out->capability, sizeof(out->capability), "gnss.fix");
    return true;
}
static bool mock_installed(const char *id, char *version, size_t capacity) {
    if (strcmp(id, "gps-nmea") || !installed_version) return false;
    const int n = snprintf(version, capacity, "%s", installed_version);
    return n > 0 && (size_t)n < capacity;
}
static bool mock_install(uint32_t index) {
    assert(index == 0);
    ++install_calls;
    return true;
}
static void mock_render(const t5_ui_chrome_t *chrome, const t5_ui_list_row_t *items,
                        uint32_t count, int32_t selected) {
    assert(chrome && items && count == 1 && selected == 0);
    ++rendered;
}
static void mock_recovery_render(const t5_ui_chrome_t *chrome, const t5_ui_list_row_t *items,
                                 uint32_t count, int32_t selected) {
    assert(chrome && items && count >= 1 && count <= RECOVERY_LIMIT + 1u &&
           selected >= 0 && selected < (int32_t)count);
    ++recovery_rendered;
}
static void mock_progress_render(const t5_ui_chrome_t *chrome, const t5_ui_list_row_t *items,
                                 uint32_t count, int32_t selected) {
    assert(chrome && items && count == 3 && selected == 0);
    assert(chrome->title && strcmp(chrome->title, "Driver installation") == 0);
    assert(items[0].title && items[0].subtitle && items[0].value &&
           items[1].subtitle && items[2].subtitle && chrome->status);
    ++progress_rendered;
    snprintf(progress_current, sizeof(progress_current), "%s", items[0].title);
    snprintf(progress_phase, sizeof(progress_phase), "%s", items[0].subtitle);
    snprintf(progress_value, sizeof(progress_value), "%s", items[0].value);
    snprintf(progress_file, sizeof(progress_file), "%s", items[1].subtitle);
    snprintf(progress_previous, sizeof(progress_previous), "%s", items[2].subtitle);
    snprintf(progress_status, sizeof(progress_status), "%s", chrome->status);
}

const t5_app_api_v1 *t5_app_get_api(uint32_t version) {
    (void)version;
    return NULL;
}
const t5_driver_manager_api_v1 *t5_driver_manager_get_api(uint32_t version) {
    (void)version;
    return NULL;
}
const t5_package_manager_api_v1 *t5_package_manager_get_api(uint32_t version) {
    (void)version;
    return NULL;
}
const t5_ui_api_v1 *t5_ui_get_api(uint32_t version) {
    (void)version;
    return NULL;
}

int main(void) {
    const t5_driver_manager_api_v1 manager = {
        .api_version = T5_DRIVER_MANAGER_API_VERSION,
        .struct_size = sizeof(t5_driver_manager_api_v1),
        .catalog_refresh = mock_catalog_refresh,
        .catalog_count = mock_count,
        .catalog_get = mock_get,
        .installed_version_get = mock_installed,
        .install = mock_install,
        .recovery_refresh = mock_recovery_refresh,
        .recovery_count = mock_recovery_count,
        .recovery_get = mock_recovery_get,
        .recovery_retry = mock_recovery_retry,
        .recovery_discard = mock_recovery_discard,
    };
    const t5_ui_api_v1 ui = {.render_list = mock_render};
    const t5_ui_api_v1 recovery_ui = {
        .render_list = mock_recovery_render,
        .poll_event = mock_poll,
        .next_index = mock_next,
        .previous_index = mock_previous,
        .hit_test = mock_hit,
    };
    char status[STATUS_BYTES] = {0};
    assert(recovery_api(&manager));
    assert(!progress_api(&manager));
    t5_driver_manager_api_v1 legacy = manager;
    legacy.struct_size = offsetof(t5_driver_manager_api_v1, recovery_refresh);
    assert(driver_api(&legacy) && !recovery_api(&legacy) && !progress_api(&legacy));
    assert(t5_package_version_compare("1.10.0", "1.9.99") == 1);
    assert(t5_package_version_compare("4294967295.0.0", "1.0.0") == 1);
    assert(t5_package_version_compare("4294967296.0.0", "1.0.0") == 2);
    assert(t5_package_version_compare("01.0.0", "1.0.0") == 0);

    source = ONLINE;
    catalog_version = "1.10.0";
    installed_version = "1.9.99";
    assert(load_release(&manager, &ui));
    assert(row_count == 1 && strstr(descriptions[0], "Installed 1.9.99"));
    assert(rows[0].flags == T5_UI_LIST_HIGHLIGHT_VALUE);
    assert(strcmp(action_label(&manager, 0), "Update") == 0);
    activate(&manager, NULL, &ui, 0, status, sizeof(status));
    // Legacy firmware now shows a one-row busy screen in addition to loading.
    assert(install_calls == 1 && rendered == 2);

    installed_version = "1.10.0";
    assert(load_release(&manager, &ui));
    assert(strstr(descriptions[0], "Installed") && !strcmp(action_label(&manager, 0), ""));
    activate(&manager, NULL, &ui, 0, status, sizeof(status));
    assert(install_calls == 1);

    installed_version = "2.0.0";
    assert(load_release(&manager, &ui));
    assert(strstr(descriptions[0], "Installed newer 2.0.0"));
    assert(!strcmp(action_label(&manager, 0), ""));
    activate(&manager, NULL, &ui, 0, status, sizeof(status));
    assert(strstr(status, "is newer") && install_calls == 1);

    installed_version = "invalid";
    assert(load_release(&manager, &ui));
    assert(strstr(descriptions[0], "version invalid"));
    assert(!strcmp(action_label(&manager, 0), ""));
    activate(&manager, NULL, &ui, 0, status, sizeof(status));
    assert(install_calls == 1);

    installed_version = NULL;
    assert(load_release(&manager, &ui));
    assert(strstr(descriptions[0], "Not installed"));
    assert(!strcmp(action_label(&manager, 0), "Install"));

    reset_queue(); enqueue(T5_UI_EVENT_CONFIRM);
    assert(!confirm(&recovery_ui, "gps-nmea", "Discard retained files"));
    reset_queue(); enqueue(T5_UI_EVENT_BACK);
    assert(!confirm(&recovery_ui, "gps-nmea", "Discard retained files"));
    reset_queue(); enqueue(T5_UI_EVENT_NEXT); enqueue(T5_UI_EVENT_CONFIRM);
    assert(confirm(&recovery_ui, "gps-nmea", "Discard retained files"));

    retained_kind = T5_DRIVER_RECOVERY_DOWNLOAD;
    retained_state = T5_DRIVER_RECOVERY_INCOMPLETE;
    retained_retry = false; retained_discard = true; retained_count = 1;
    reset_queue();
    enqueue(T5_UI_EVENT_CONFIRM);
    enqueue(T5_UI_EVENT_CONFIRM); // Action menu defaults to Keep.
    enqueue(T5_UI_EVENT_BACK);
    assert(recovery_screen(&manager, &recovery_ui));
    assert(recovery_discards == 0 && retained_count == 1);

    reset_queue();
    enqueue(T5_UI_EVENT_CONFIRM);
    enqueue(T5_UI_EVENT_NEXT); enqueue(T5_UI_EVENT_CONFIRM); // Choose discard.
    enqueue(T5_UI_EVENT_NEXT); enqueue(T5_UI_EVENT_CONFIRM); // Confirm separately.
    enqueue(T5_UI_EVENT_CONFIRM); // Continue after refresh reports no stage.
    assert(recovery_screen(&manager, &recovery_ui));
    assert(recovery_discards == 1 && retained_count == 0);

    retained_kind = T5_DRIVER_RECOVERY_STAGE;
    retained_state = T5_DRIVER_RECOVERY_READY;
    retained_retry = true; retained_discard = true; retained_count = 1;
    reset_queue();
    enqueue(T5_UI_EVENT_CONFIRM);
    enqueue(T5_UI_EVENT_NEXT); enqueue(T5_UI_EVENT_CONFIRM); // Retry stage.
    enqueue(T5_UI_EVENT_CONFIRM);
    assert(recovery_screen(&manager, &recovery_ui));
    assert(recovery_retries == 1 && recovery_discards == 1 && retained_count == 0);

    retained_state = T5_DRIVER_RECOVERY_MAPPED;
    retained_retry = false; retained_discard = false; retained_count = 1;
    reset_queue();
    enqueue(T5_UI_EVENT_CONFIRM); // Open actions: only Cancel is available.
    enqueue(T5_UI_EVENT_BACK);    // Cancel the nested action menu.
    enqueue(T5_UI_EVENT_BACK);    // Then leave the recovery screen.
    assert(recovery_screen(&manager, &recovery_ui));
    assert(recovery_retries == 1 && recovery_discards == 1);
    assert(recovery_rendered > 0 && recovery_refreshes > 0);

    // The actual app must render the installer events on the device, not just
    // log them. Unknown download lengths must never display a percentage.
    const t5_ui_api_v1 progress_ui = {.render_list = mock_progress_render};
    begin_install_progress(NULL, &progress_ui, "usb-host-v2");
    assert(progress_rendered == 1 && !strcmp(progress_current, "usb-host-v2"));
    assert(strstr(progress_status, "Working") != NULL);
    const t5_driver_install_event_t dependency = {
        "i2c-esp32s3-v2", NULL, T5_DRIVER_INSTALL_DEPENDENCY, 0, 0};
    install_progress(&install_view, &dependency);
    assert(!strcmp(progress_current, "i2c-esp32s3-v2"));
    assert(!strcmp(progress_phase, "Required dependency"));
    const t5_driver_install_event_t downloading = {
        "i2c-esp32s3-v2", "driver.elf", T5_DRIVER_INSTALL_DOWNLOADING, 250, 1000};
    install_progress(&install_view, &downloading);
    assert(strstr(progress_value, "25%") && !strcmp(progress_file, "driver.elf"));
    assert(strstr(progress_previous, "Required dependency"));
    const t5_driver_install_event_t verifying = {
        "i2c-esp32s3-v2", NULL, T5_DRIVER_INSTALL_VERIFYING, 0, 0};
    install_progress(&install_view, &verifying);
    assert(strstr(progress_phase, "Verifying") && !progress_value[0]);
    const t5_driver_install_event_t unknown_size = {
        "i2c-esp32s3-v2", "provider-abi.v1", T5_DRIVER_INSTALL_DOWNLOADING, 512, 0};
    install_progress(&install_view, &unknown_size);
    assert(strstr(progress_value, "512 bytes received") && !strchr(progress_value, '%'));
    const t5_driver_install_event_t failed = {
        "usb-host-v2", NULL, T5_DRIVER_INSTALL_FAILED, 0, 0};
    install_progress(&install_view, &failed);
    assert(!strcmp(progress_current, "usb-host-v2") && strstr(progress_status, "Failed"));
    assert(progress_rendered >= 5);

    puts("Driver Manager real UI: versions, progress events/bytes, legacy busy screen, offline recovery and safe discard PASS");
    return 0;
}
