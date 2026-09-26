#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../Apps/package_manager.c"

static t5_ui_event_t events[32];
static unsigned total, next_event, installed, replaced, uninstalled, rendered;
static bool uninstall_ok = true;
static bool replace_ok = true;

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

static bool installed_refresh_mock(void) { return true; }
static uint32_t installed_count_mock(void) { return 1u; }
static bool installed_get_mock(uint32_t index, t5_installed_package_t *out) {
    if (index != 0u || !out) return false;
    memset(out, 0, sizeof(*out));
    out->kind = T5_PACKAGE_DRIVER;
    out->valid_installation = 1;
    snprintf(out->id, sizeof(out->id), "gps-nmea");
    snprintf(out->version, sizeof(out->version), "2.0.0");
    snprintf(out->artifact, sizeof(out->artifact), "driver.elf");
    return true;
}
static bool preview_mock(const char* folder, t5_package_preview_t *out) {
    if (!folder || !out || strcmp(folder, "gps-nmea")) return false;
    memset(out, 0, sizeof(*out));
    out->kind = T5_PACKAGE_DRIVER;
    out->valid_installation = 1;
    out->install_allowed = 0; /* Older versions are NOT normal upgrades. */
    snprintf(out->id, sizeof(out->id), "gps-nmea");
    snprintf(out->version, sizeof(out->version), "1.5.0");
    snprintf(out->artifact, sizeof(out->artifact), "driver.elf");
    snprintf(out->installed_version, sizeof(out->installed_version), "2.0.0");
    return true;
}
static bool install_mock(const char* folder) {
    assert(folder && !strcmp(folder, "fresh-app"));
    ++installed;
    return true;
}
static bool replace_mock(const char* folder) {
    assert(folder && !strcmp(folder, "gps-nmea"));
    ++replaced;
    return replace_ok;
}
static bool uninstall_mock(uint8_t kind, const char* id) {
    assert(kind == T5_PACKAGE_DRIVER && id && !strcmp(id, "gps-nmea"));
    ++uninstalled;
    return uninstall_ok;
}

static bool dir_open_none(const char *path) {
    assert(path && !strcmp(path, "/sd/Packages/Inbox"));
    return false;
}
static bool dir_next_none(t5_app_dirent_t *entry) {
    (void)entry;
    return false;
}
static void dir_close_none(void) {}

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
    const t5_app_api_v1 app = {
        .abi_version = T5_APP_ABI_VERSION,
        .struct_size = sizeof(t5_app_api_v1),
        .dir_open = dir_open_none,
        .dir_next = dir_next_none,
        .dir_close = dir_close_none,
    };
    const t5_package_manager_api_v1 manager = {
        .api_version = T5_PACKAGE_MANAGER_API_VERSION,
        .struct_size = sizeof(t5_package_manager_api_v1),
        .preview = preview_mock,
        .install = install_mock,
        .uninstall = uninstall_mock,
        .installed_refresh = installed_refresh_mock,
        .installed_count = installed_count_mock,
        .installed_get = installed_get_mock,
        .replace = replace_mock,
    };

    assert(api_ready(&manager, &ui));
    t5_package_manager_api_v1 old = manager;
    old.struct_size = offsetof(t5_package_manager_api_v1, installed_refresh);
    assert(!api_ready(&old, &ui));

    refresh(&app, &manager);
    assert(row_count == 1u);
    assert(packages[0].source == PACKAGE_ROW_INSTALLED);
    assert(packages[0].has_staged);
    assert(!strcmp(packages[0].installed.version, "2.0.0"));
    assert(!strcmp(packages[0].staged.version, "1.5.0"));
    assert(strstr(subtitles[0], "older"));

    char status[STATUS_BYTES] = {0};

    // Default action remains Cancel.
    reset_events(); add(T5_UI_EVENT_CONFIRM);
    activate(&manager, &ui, 0, status, sizeof(status));
    assert(replaced == 0 && uninstalled == 0);

    // Explicitly choose the older staged version and approve the downgrade.
    reset_events();
    add(T5_UI_EVENT_NEXT); add(T5_UI_EVENT_CONFIRM);
    add(T5_UI_EVENT_NEXT); add(T5_UI_EVENT_CONFIRM);
    activate(&manager, &ui, 0, status, sizeof(status));
    assert(replaced == 1);
    assert(strstr(status, "downgraded"));
    assert(strstr(status, "2.0.0 -> 1.5.0"));

    // Explicit uninstall is a separate action and confirmation.
    reset_events();
    add(T5_UI_EVENT_NEXT); add(T5_UI_EVENT_NEXT); add(T5_UI_EVENT_CONFIRM);
    add(T5_UI_EVENT_NEXT); add(T5_UI_EVENT_CONFIRM);
    activate(&manager, &ui, 0, status, sizeof(status));
    assert(uninstalled == 1);
    assert(strstr(status, "uninstalled"));

    // Firmware refusal is visible and does not masquerade as success.
    uninstall_ok = false;
    reset_events();
    add(T5_UI_EVENT_NEXT); add(T5_UI_EVENT_NEXT); add(T5_UI_EVENT_CONFIRM);
    add(T5_UI_EVENT_NEXT); add(T5_UI_EVENT_CONFIRM);
    activate(&manager, &ui, 0, status, sizeof(status));
    assert(uninstalled == 2);
    assert(strstr(status, "refused"));

    // Inbox-only packages preserve the old fresh-install workflow.
    row_count = 1u;
    memset(&packages[0], 0, sizeof(packages[0]));
    packages[0].source = PACKAGE_ROW_INBOX;
    packages[0].has_staged = 1;
    packages[0].staged.kind = T5_PACKAGE_APPLICATION;
    packages[0].staged.valid_installation = 1;
    packages[0].staged.install_allowed = 1;
    snprintf(packages[0].staged.id, sizeof(packages[0].staged.id), "fresh-app");
    snprintf(packages[0].staged.version, sizeof(packages[0].staged.version), "1.0.0");
    reset_events();
    add(T5_UI_EVENT_NEXT); add(T5_UI_EVENT_CONFIRM);
    add(T5_UI_EVENT_NEXT); add(T5_UI_EVENT_CONFIRM);
    activate(&manager, &ui, 0, status, sizeof(status));
    assert(installed == 1);
    assert(strstr(status, "installed"));

    assert(rendered > 0);
    puts("Package Manager UI: installed inventory, explicit downgrade, uninstall, refusal and fresh install PASS");
    return 0;
}
