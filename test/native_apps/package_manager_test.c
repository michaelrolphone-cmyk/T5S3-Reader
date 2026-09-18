#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../Apps/package_manager.c"

static t5_ui_event_t events[24];
static unsigned total, next_event, installed, uninstalled, rendered;
static bool uninstall_ok;
static void reset_events(void) { total = next_event = 0; }
static void add(uint8_t kind) {
    assert(total < sizeof(events) / sizeof(events[0]));
    events[total++] = (t5_ui_event_t){.type = kind};
}
static bool poll(t5_ui_event_t *event, uint32_t timeout) {
    (void)timeout;
    if (next_event >= total) return false;
    *event = events[next_event++];
    return true;
}
static int32_t increment(int32_t index, uint32_t count) {
    return count ? (index + 1) % (int32_t)count : 0;
}
static int32_t decrement(int32_t index, uint32_t count) {
    return count ? (index + (int32_t)count - 1) % (int32_t)count : 0;
}
static int32_t hit(int16_t x, int16_t y) { (void)y; return x; }
static void render_mock(const t5_ui_chrome_t *chrome,
                        const t5_ui_list_row_t *list,
                        uint32_t count, int32_t selected) {
    assert(chrome && list && count && selected >= 0 && selected < (int32_t)count);
    ++rendered;
}
static bool install_mock(const char* folder) {
    assert(!strcmp(folder, "gps-nmea"));
    ++installed;
    return true;
}
static bool uninstall_mock(uint8_t kind, const char* id) {
    assert(kind == T5_PACKAGE_DRIVER && !strcmp(id, "gps-nmea"));
    ++uninstalled;
    return uninstall_ok;
}
static bool preview_mock(const char* folder, t5_package_preview_t *out) {
    (void)folder; (void)out;
    return false;
}
const t5_app_api_v1 *t5_app_get_api(uint32_t version) {
    (void)version; return NULL;
}
const t5_ui_api_v1 *t5_ui_get_api(uint32_t version) {
    (void)version; return NULL;
}
const t5_package_manager_api_v1 *t5_package_manager_get_api(uint32_t version) {
    (void)version; return NULL;
}
int main(void) {
    const t5_ui_api_v1 ui = {
        .api_version = T5_UI_API_VERSION, .struct_size = sizeof(t5_ui_api_v1),
        .render_list = render_mock, .poll_event = poll,
        .next_index = increment, .previous_index = decrement, .hit_test = hit,
    };
    const t5_package_manager_api_v1 manager = {
        .api_version = T5_PACKAGE_MANAGER_API_VERSION,
        .struct_size = sizeof(t5_package_manager_api_v1),
        .preview = preview_mock, .install = install_mock,
        .uninstall = uninstall_mock,
    };
    assert(api_ready(&manager, &ui));
    t5_package_manager_api_v1 old = manager;
    old.struct_size = offsetof(t5_package_manager_api_v1, uninstall);
    assert(!api_ready(&old, &ui));
    row_count = 1;
    packages[0] = (t5_package_preview_t){0};
    packages[0].kind = T5_PACKAGE_DRIVER;
    packages[0].valid_installation = 1;
    packages[0].install_allowed = 1;
    snprintf(packages[0].id, sizeof(packages[0].id), "gps-nmea");
    snprintf(packages[0].version, sizeof(packages[0].version), "1.0.1");
    snprintf(packages[0].installed_version, sizeof(packages[0].installed_version), "1.0.0");
    char status[STATUS_BYTES] = {0};

    // First Confirm on the action menu is Cancel; no implicit uninstall.
    reset_events(); add(T5_UI_EVENT_CONFIRM);
    activate(&manager, &ui, 0, status, sizeof(status));
    assert(installed == 0 && uninstalled == 0);
    // Select update, then cancel the separate final confirmation.
    reset_events(); add(T5_UI_EVENT_NEXT); add(T5_UI_EVENT_CONFIRM);
    add(T5_UI_EVENT_CONFIRM);
    activate(&manager, &ui, 0, status, sizeof(status));
    assert(installed == 0 && uninstalled == 0);
    // Select update, approve its separate confirmation.
    reset_events(); add(T5_UI_EVENT_NEXT); add(T5_UI_EVENT_CONFIRM);
    add(T5_UI_EVENT_NEXT); add(T5_UI_EVENT_CONFIRM);
    activate(&manager, &ui, 0, status, sizeof(status));
    assert(installed == 1 && uninstalled == 0);
    // Explicitly select uninstall, cancel confirmation: no removal.
    reset_events(); add(T5_UI_EVENT_NEXT); add(T5_UI_EVENT_NEXT);
    add(T5_UI_EVENT_CONFIRM); add(T5_UI_EVENT_BACK);
    activate(&manager, &ui, 0, status, sizeof(status));
    assert(installed == 1 && uninstalled == 0);
    // Explicitly select uninstall, approve; firmware can still refuse in-use.
    reset_events(); add(T5_UI_EVENT_NEXT); add(T5_UI_EVENT_NEXT);
    add(T5_UI_EVENT_CONFIRM); add(T5_UI_EVENT_NEXT); add(T5_UI_EVENT_CONFIRM);
    activate(&manager, &ui, 0, status, sizeof(status));
    assert(uninstalled == 1 && strstr(status, "refused"));
    uninstall_ok = true;
    reset_events(); add(T5_UI_EVENT_NEXT); add(T5_UI_EVENT_NEXT);
    add(T5_UI_EVENT_CONFIRM); add(T5_UI_EVENT_NEXT); add(T5_UI_EVENT_CONFIRM);
    activate(&manager, &ui, 0, status, sizeof(status));
    assert(uninstalled == 2 && strstr(status, "uninstalled"));
    packages[0].valid_installation = 0;
    reset_events();
    activate(&manager, &ui, 0, status, sizeof(status));
    assert(installed == 1 && uninstalled == 2);
    assert(rendered > 0);
    puts("Package Manager real UI: default cancel, independent install/uninstall, separate confirmation and firmware refusal PASS");
    return 0;
}
