#include <assert.h>
#include <stdio.h>
#include <string.h>

// Compile the real app, including its static action and row-building paths.
#include "../../Apps/driver_manager.c"

static const char *catalog_version;
static const char *installed_version;
static unsigned install_calls;
static unsigned rendered;

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

const t5_app_api_v1 *t5_app_get_api(uint32_t version) {
    (void)version;
    return NULL;
}
const t5_driver_manager_api_v1 *t5_driver_manager_get_api(uint32_t version) {
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
        .catalog_count = mock_count,
        .catalog_get = mock_get,
        .installed_version_get = mock_installed,
        .install = mock_install,
    };
    const t5_ui_api_v1 ui = {.render_list = mock_render};
    char status[STATUS_MAX] = {0};

    assert(t5_package_version_compare("1.10.0", "1.9.99") == 1);
    assert(t5_package_version_compare("4294967295.0.0", "1.0.0") == 1);
    assert(t5_package_version_compare("4294967296.0.0", "1.0.0") == 2);
    assert(t5_package_version_compare("01.0.0", "1.0.0") == 0);

    catalog_version = "1.10.0";
    installed_version = "1.9.99";
    build_rows(&manager); // Previously returned INSTALL because row_count was still zero.
    assert(row_count == 1 && strstr(row_subtitles[0], "Installed 1.9.99"));
    assert(rows[0].flags == T5_UI_LIST_HIGHLIGHT_VALUE);
    assert(strcmp(confirm_label(&manager, 0), "Update") == 0);
    activate_selected(&manager, &ui, 0, status, sizeof(status));
    assert(install_calls == 1 && rendered == 1);

    installed_version = "1.10.0";
    build_rows(&manager);
    assert(strstr(row_subtitles[0], "Installed") && !strcmp(confirm_label(&manager, 0), ""));
    activate_selected(&manager, &ui, 0, status, sizeof(status));
    assert(install_calls == 1);

    installed_version = "2.0.0";
    build_rows(&manager);
    assert(strstr(row_subtitles[0], "Installed newer 2.0.0"));
    assert(!strcmp(confirm_label(&manager, 0), ""));
    activate_selected(&manager, &ui, 0, status, sizeof(status));
    assert(strstr(status, "newer than catalog") && install_calls == 1);

    installed_version = "invalid";
    build_rows(&manager);
    assert(strstr(row_subtitles[0], "version invalid"));
    assert(!strcmp(confirm_label(&manager, 0), ""));
    activate_selected(&manager, &ui, 0, status, sizeof(status));
    assert(install_calls == 1);

    installed_version = NULL;
    build_rows(&manager);
    assert(strstr(row_subtitles[0], "Not installed"));
    assert(!strcmp(confirm_label(&manager, 0), "Install"));
    puts("Driver Manager real UI: installed rows, numeric update, equal/newer refusal, invalid version and fresh install PASS");
    return 0;
}
