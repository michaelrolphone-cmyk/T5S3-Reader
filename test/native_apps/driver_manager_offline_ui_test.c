#include "T5AppApi.h"
#include "T5DriverManagerApi.h"
#include "T5DriverOfflineApi.h"
#include "T5UiApi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned local_refreshes, online_refreshes, installs, renders;
static unsigned last_installed;
static t5_ui_event_type_t events[12];
static unsigned event_count, event_next;
static bool found_installed, found_inbox, found_online_failure;

static void back_exit(bool enabled) { (void)enabled; }
static const t5_app_api_v1 app_api = { .abi_version = T5_APP_ABI_VERSION,
    .struct_size = sizeof(t5_app_api_v1), .set_back_exits_app = back_exit };
const t5_app_api_v1 *t5_app_get_api(uint32_t v) { return v == T5_APP_ABI_VERSION ? &app_api : NULL; }

static bool offline_refresh(void) { ++local_refreshes; return true; }
static uint32_t offline_count(void) { return 2; }
static bool offline_get(uint32_t index, t5_driver_local_entry_t *out) {
    if (!out || index >= 2) return false;
    *out = (t5_driver_local_entry_t){0};
    snprintf(out->id, sizeof(out->id), "%s", index ? "usb-cdc-acm" : "gps-nmea");
    snprintf(out->version, sizeof(out->version), "%s", index ? "0.1.0" : "1.0.0");
    snprintf(out->capability, sizeof(out->capability), "%s", index ? "usb.class.cdc_acm" : "position.gnss");
    out->flags = T5_DRIVER_LOCAL_VALID;
    out->source = index ? T5_DRIVER_LOCAL_INBOX : T5_DRIVER_LOCAL_INSTALLED;
    return true;
}
static bool offline_install(uint32_t index) { ++installs; last_installed = index; return index == 1; }
static const t5_driver_offline_api_v1 offline_api = {
    T5_DRIVER_OFFLINE_API_VERSION, sizeof(t5_driver_offline_api_v1),
    offline_refresh, offline_count, offline_get, offline_install
};
const t5_driver_offline_api_v1 *t5_driver_offline_get_api(uint32_t v) {
    return v == T5_DRIVER_OFFLINE_API_VERSION ? &offline_api : NULL;
}

static bool online_refresh(void) { ++online_refreshes; return false; }
static uint32_t online_count(void) { return 0; }
static bool online_get(uint32_t i, t5_driver_catalog_entry_t *out) { (void)i; (void)out; return false; }
static bool installed_version(const char *id, char *version, size_t size) {
    (void)id; if (version && size) version[0] = 0; return false;
}
static bool online_install(uint32_t index) { (void)index; assert(!"Unexpected online install"); return false; }
static const t5_driver_manager_api_v1 online_api = {
    T5_DRIVER_MANAGER_API_VERSION, sizeof(t5_driver_manager_api_v1),
    online_refresh, online_count, online_get, installed_version, online_install
};
const t5_driver_manager_api_v1 *t5_driver_manager_get_api(uint32_t v) {
    return v == T5_DRIVER_MANAGER_API_VERSION ? &online_api : NULL;
}

static void render(const t5_ui_chrome_t *chrome, const t5_ui_list_row_t *rows,
                   uint32_t count, int32_t selected) {
    (void)selected;
    assert(chrome && rows && count >= 2);
    ++renders;
    if (!strcmp(chrome->subtitle, "SD / offline")) {
        assert(count == 4);
        found_installed |= !strcmp(rows[2].title, "gps-nmea");
        found_inbox |= !strcmp(rows[3].title, "usb-cdc-acm");
    }
    if (strstr(chrome->status, "Release unavailable")) found_online_failure = true;
}
static int32_t hit(int16_t x, int16_t y) { (void)x; (void)y; return T5_UI_HIT_NONE; }
static bool poll(t5_ui_event_t *event, uint32_t timeout) {
    (void)timeout;
    if (event_next >= event_count) return false;
    *event = (t5_ui_event_t){0};
    event->type = events[event_next++];
    return true;
}
static int32_t next(int32_t current, uint32_t count) {
    return count ? (current + 1) % (int32_t)count : 0;
}
static int32_t previous(int32_t current, uint32_t count) {
    return count ? (current + (int32_t)count - 1) % (int32_t)count : 0;
}
static const t5_ui_api_v1 ui_api = {
    .api_version = T5_UI_API_VERSION, .struct_size = sizeof(t5_ui_api_v1),
    .render_list = render, .hit_test = hit, .poll_event = poll,
    .next_index = next, .previous_index = previous
};
const t5_ui_api_v1 *t5_ui_get_api(uint32_t v) { return v == T5_UI_API_VERSION ? &ui_api : NULL; }
static void reset(void) {
    local_refreshes = online_refreshes = installs = renders = 0;
    last_installed = 0; event_count = event_next = 0;
    found_installed = found_inbox = found_online_failure = false;
}
static void push(t5_ui_event_type_t event) { assert(event_count < 12); events[event_count++] = event; }
int main(void) {
    reset();
    push(T5_UI_EVENT_EXIT);
    app_main();
    assert(local_refreshes == 1 && online_refreshes == 0 && installs == 0);
    assert(found_installed && found_inbox && renders >= 1);

    reset();
    push(T5_UI_EVENT_NEXT); push(T5_UI_EVENT_NEXT); push(T5_UI_EVENT_NEXT);
    push(T5_UI_EVENT_CONFIRM); push(T5_UI_EVENT_EXIT);
    app_main();
    assert(installs == 1 && last_installed == 1 && online_refreshes == 0);

    reset();
    push(T5_UI_EVENT_CONFIRM); // Explicitly enter online view; simulated network failure.
    push(T5_UI_EVENT_BACK);    // Must return to local inventory, not terminate app.
    push(T5_UI_EVENT_EXIT);
    app_main();
    assert(online_refreshes == 1 && local_refreshes >= 2);
    assert(found_online_failure && found_installed && found_inbox);
    puts("Driver Manager offline UI tests passed");
    return 0;
}
