#include "T5StreamApi.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "NativeAppLauncher.h"
#include "esp_dlfcn.h"
#include "esp_elf.h"
#include "T5AppApi.h"
#include "T5ArchiveApi.h"
#include "T5BatteryApi.h"
#include "T5ButtonRemapApi.h"
#include "T5CacheApi.h"
#include "T5DeviceApi.h"
#include "T5DriverManagerApi.h"
#include "T5FileBrowserApi.h"
#include "T5FileOpenApi.h"
#include "T5FontApi.h"
#include "T5GpsApi.h"
#include "T5HardwareTakeover.h"
#include "T5ImageApi.h"
#include "T5KOReaderApi.h"
#include "T5LanguageApi.h"
#include "T5LoRaApi.h"
#include "T5NetworkApi.h"
#include "T5OpdsApi.h"
#include "T5OtaApi.h"
#include "T5SdFirmwareApi.h"
#include "T5SerialPortApi.h"
#include "T5StatusBarApi.h"
#include "T5StorageApi.h"
#include "T5SystemApi.h"
#include "T5SystemUiApi.h"
#include "T5TimeZoneApi.h"
#include "T5UiApi.h"
#include "T5UsbApi.h"
#include "T5WebServerApi.h"

extern int test_capability_gate_allowed;
extern int test_capability_bind_allowed;
extern int test_capability_bind_calls;
static int mode, opens, closes, calls, handle_storage;
static const char *pending;
static bool running;
static bool takeover_exported;
static bool takeover_active;
static bool takeover_denied;
static bool restore_failed;
static uint32_t takeover_request;
static int takeover_begins, takeover_ends;
static bool lifecycle_init_exported, lifecycle_fini_exported, lifecycle_init_fails;
static bool lifecycle_fini_saw_takeover;
static int lifecycle_inits, lifecycle_finis;

const char *dlerror(void) { const char *e = pending; pending = NULL; return e; }

static uint32_t declare_takeover(void) { return takeover_request; }
static int module_init(void) { ++lifecycle_inits; return lifecycle_init_fails ? -1 : 0; }
static void module_fini(void) { ++lifecycle_finis; lifecycle_fini_saw_takeover = takeover_active; }
static void child(void)
{
    running = true;
    ++calls;
    assert(closes == 0);
    assert(native_app_current_path() != NULL);
    assert(strcmp(native_app_current_path(), "/sd/apps/game.elf") == 0);
    assert(takeover_active == (takeover_exported && takeover_request != 0U));
    assert(launch_elf_app("/sd/apps/nested.elf") == ESP_ERR_INVALID_STATE);
    assert(strcmp(native_app_current_path(), "/sd/apps/game.elf") == 0);
    running = false;
}
void *dlopen(const char *path, int flags)
{
    ++opens;
    assert(strcmp(path, "/sd/apps/game.elf") == 0 && flags == RTLD_NOW);
    assert(pending == NULL);
    if (mode == 1) { pending = "read/relocation/allocation failed"; return NULL; }
    return &handle_storage;
}
void *dlsym(void *handle, const char *name)
{
    assert(handle == &handle_storage);
    if (strcmp(name, "app_hardware_takeover") == 0) {
        if (!takeover_exported) { pending = "optional symbol absent"; return NULL; }
        return (void *)declare_takeover;
    }
    if (strcmp(name, "app_module_init") == 0) {
        if (!lifecycle_init_exported) { pending = "optional symbol absent"; return NULL; }
        return (void *)module_init;
    }
    if (strcmp(name, "app_module_fini") == 0) {
        if (!lifecycle_fini_exported) { pending = "optional symbol absent"; return NULL; }
        return (void *)module_fini;
    }
    assert(strcmp(name, "app_main") == 0);
    if (mode == 2) { pending = "symbol missing"; return NULL; }
    if (mode == 3) return NULL;
    if (mode == 5) pending = "non-null symbol with loader error";
    return (void *)child;
}
int dlclose(void *handle)
{
    assert(handle == &handle_storage && !running && !takeover_active);
    ++closes;
    if (mode == 4) { pending = "close failed"; return -1; }
    return 0;
}

// Isolated host-side models of the real firmware-side relinquish/restore hooks.
// The C++ bridge and T5S3 bus teardown are also checked by firmware builds.
esp_err_t native_hardware_takeover_begin(uint32_t mask)
{
    ++takeover_begins;
    assert(!takeover_active);
    if ((mask & ~T5_HARDWARE_TAKEOVER_SUPPORTED) != 0U) return ESP_ERR_NOT_SUPPORTED;
    if (takeover_denied) return ESP_ERR_INVALID_STATE;
    takeover_active = true;
    return ESP_OK;
}
esp_err_t native_hardware_takeover_end(uint32_t mask)
{
    ++takeover_ends;
    assert(mask == T5_HARDWARE_TAKEOVER_DISPLAY && takeover_active && !running);
    takeover_active = false;
    return restore_failed ? ESP_FAIL : ESP_OK;
}

static void reset_takeover(void)
{
    takeover_exported = false;
    takeover_active = false;
    takeover_denied = false;
    restore_failed = false;
    takeover_request = 0U;
    takeover_begins = takeover_ends = 0;
    lifecycle_init_exported = lifecycle_fini_exported = lifecycle_init_fails = false;
    lifecycle_fini_saw_takeover = false;
    lifecycle_inits = lifecycle_finis = 0;
    opens = closes = calls = 0;
    mode = 0;
}

int main(void)
{
    assert(native_app_current_path() == NULL);
    assert(launch_elf_app(NULL) == ESP_ERR_INVALID_ARG);
    assert(launch_elf_app("") == ESP_ERR_INVALID_ARG);
    assert(launch_elf_app("/") == ESP_ERR_INVALID_ARG);
    assert(launch_elf_app("relative.elf") == ESP_ERR_INVALID_ARG);
    assert(native_app_current_path() == NULL);
    assert(opens == 0);
    for (int round = 0; round < 3; ++round) {
        for (mode = 0; mode < 6; ++mode) {
            opens = closes = calls = 0;
            pending = "stale error";
            int rc = launch_elf_app("/sd/apps/game.elf");
            assert(native_app_current_path() == NULL);
            assert(opens == 1);
            assert(closes == (mode == 1 ? 0 : 1));
            assert(calls == (mode == 0 || mode == 4 ? 1 : 0));
            assert(rc == (mode == 0 ? ESP_OK : (mode == 2 || mode == 3 || mode == 5)
                          ? ESP_ERR_NOT_FOUND : ESP_FAIL));
            assert(takeover_begins == 0 && takeover_ends == 0);
        }
    }
    // Preflight denial cannot allocate bindings; binding denial must reject
    // BEFORE registering symbols, dlopen or app_main. Neither locks next launch.
    test_capability_gate_allowed = 0;
    opens = closes = calls = test_capability_bind_calls = 0;
    assert(launch_elf_app("/sd/apps/game.elf") == ESP_ERR_NOT_SUPPORTED);
    assert(opens == 0 && closes == 0 && calls == 0 && test_capability_bind_calls == 0);
    assert(native_app_current_path() == NULL);
    test_capability_gate_allowed = 1;
    test_capability_bind_allowed = 0;
    assert(launch_elf_app("/sd/apps/game.elf") == ESP_ERR_NOT_SUPPORTED);
    assert(opens == 0 && closes == 0 && calls == 0 && test_capability_bind_calls == 1);
    assert(native_app_current_path() == NULL);
    test_capability_bind_allowed = 1;
    mode = 0;
    assert(launch_elf_app("/sd/apps/game.elf") == ESP_OK);
    assert(opens == 1 && closes == 1 && calls == 1 && test_capability_bind_calls == 2);

    // Explicit zero request leaves the usual display owner untouched.
    reset_takeover();
    takeover_exported = true;
    assert(launch_elf_app("/sd/apps/game.elf") == ESP_OK);
    assert(calls == 1 && takeover_begins == 0 && takeover_ends == 0);

    reset_takeover();
    takeover_exported = true;
    takeover_request = T5_HARDWARE_TAKEOVER_DISPLAY;
    assert(launch_elf_app("/sd/apps/game.elf") == ESP_OK);
    assert(calls == 1 && closes == 1 && takeover_begins == 1 && takeover_ends == 1);
    assert(!takeover_active && native_app_current_path() == NULL);

    // A busy host refuses takeover and does not call the ELF entry point.
    reset_takeover();
    takeover_exported = true;
    takeover_request = T5_HARDWARE_TAKEOVER_DISPLAY;
    takeover_denied = true;
    assert(launch_elf_app("/sd/apps/game.elf") == ESP_ERR_INVALID_STATE);
    assert(calls == 0 && closes == 1 && takeover_begins == 1 && takeover_ends == 0);
    assert(!takeover_active);

    // Paired lifecycle hooks wrap app_main and complete before host restore.
    reset_takeover();
    lifecycle_init_exported = lifecycle_fini_exported = true;
    takeover_exported = true;
    takeover_request = T5_HARDWARE_TAKEOVER_DISPLAY;
    assert(launch_elf_app("/sd/apps/game.elf") == ESP_OK);
    assert(lifecycle_inits == 1 && lifecycle_finis == 1 && calls == 1);
    assert(lifecycle_fini_saw_takeover && takeover_ends == 1 && !takeover_active);

    // A partial lifecycle contract and failed initialization both fail closed.
    reset_takeover();
    lifecycle_init_exported = true;
    assert(launch_elf_app("/sd/apps/game.elf") == ESP_ERR_INVALID_STATE);
    assert(lifecycle_inits == 0 && lifecycle_finis == 0 && calls == 0 && closes == 1);
    reset_takeover();
    lifecycle_init_exported = lifecycle_fini_exported = lifecycle_init_fails = true;
    assert(launch_elf_app("/sd/apps/game.elf") == ESP_ERR_INVALID_STATE);
    assert(lifecycle_inits == 1 && lifecycle_finis == 0 && calls == 0 && closes == 1);

    // Unsupported future bits fail closed instead of quietly granting rights.
    reset_takeover();
    takeover_exported = true;
    takeover_request = (1u << 31);
    assert(launch_elf_app("/sd/apps/game.elf") == ESP_ERR_NOT_SUPPORTED);
    assert(calls == 0 && closes == 1 && takeover_begins == 1 && takeover_ends == 0);

    // A failed host restoration is reported, not misrepresented as success.
    reset_takeover();
    takeover_exported = true;
    takeover_request = T5_HARDWARE_TAKEOVER_DISPLAY;
    restore_failed = true;
    assert(launch_elf_app("/sd/apps/game.elf") == ESP_FAIL);
    assert(calls == 1 && closes == 1 && takeover_begins == 1 && takeover_ends == 1);
    assert(!takeover_active);

    reset_takeover();
    assert(launch_elf_app("/sd/apps/game.elf") == ESP_OK);
    assert(calls == 1 && closes == 1 && takeover_begins == 0 && takeover_ends == 0);
    return 0;
}

esp_err_t native_app_register_sd_vfs(void) { return ESP_OK; }
int esp_elf_register_symbol(const struct esp_elfsym *s)
{
    assert(s);
    int count = 0;
    bool serial_port_found = false;
    bool device_found = false;
    while (s[count].name) {
        assert(s[count].sym);
        if (strcmp(s[count].name, "t5_serial_port_get_api") == 0) serial_port_found = true;
        if (strcmp(s[count].name, "t5_device_get_api") == 0) device_found = true;
        ++count;
    }
    assert(count >= 24);
    assert(serial_port_found && device_found);
    // Baseline libc helpers are resolved from g_esp_libc_elfsyms, not this
    // app-specific host API table. test_symbols.py validates that stable set.
    for (int i = 0; i < count; ++i) {
        assert(strcmp(s[i].name, "snprintf") != 0);
        assert(strcmp(s[i].name, "strcpy") != 0);
        assert(strcmp(s[i].name, "strncpy") != 0);
    }
    return 0;
}
const t5_app_api_v1 *t5_app_get_api(uint32_t version) { (void)version; return NULL; }
const t5_archive_api_v1 *t5_archive_get_api(uint32_t version) { (void)version; return NULL; }
const t5_battery_api_v1 *t5_battery_get_api(uint32_t version) { (void)version; return NULL; }
const t5_button_remap_api_v1 *t5_button_remap_get_api(uint32_t version) { (void)version; return NULL; }
const t5_cache_api_v1 *t5_cache_get_api(uint32_t version) { (void)version; return NULL; }
const t5_driver_manager_api_v1 *t5_driver_manager_get_api(uint32_t version) { (void)version; return NULL; }
const t5_file_browser_api_v1 *t5_file_browser_get_api(uint32_t version) { (void)version; return NULL; }
const t5_file_open_api_v1 *t5_file_open_get_api(uint32_t version) { (void)version; return NULL; }
const t5_font_api_v1 *t5_font_get_api(uint32_t version) { (void)version; return NULL; }
const t5_gps_api_v1 *t5_gps_get_api(uint32_t version) { (void)version; return NULL; }
const t5_image_api_v1 *t5_image_get_api(uint32_t version) { (void)version; return NULL; }
const t5_koreader_api_v1 *t5_koreader_get_api(uint32_t version) { (void)version; return NULL; }
const t5_language_api_v1 *t5_language_get_api(uint32_t version) { (void)version; return NULL; }
const t5_lora_api_v1 *t5_lora_get_api(uint32_t version) { (void)version; return NULL; }
const t5_network_api_v1 *t5_network_get_api(uint32_t version) { (void)version; return NULL; }
const t5_opds_api_v1 *t5_opds_get_api(uint32_t version) { (void)version; return NULL; }
const t5_ota_api_v1 *t5_ota_get_api(uint32_t version) { (void)version; return NULL; }
const t5_sd_firmware_api_v1 *t5_sd_firmware_get_api(uint32_t version) { (void)version; return NULL; }
const t5_serial_port_api_v1 *t5_serial_port_get_api(uint32_t version) { (void)version; return NULL; }
const t5_status_bar_api_v1 *t5_status_bar_get_api(uint32_t version) { (void)version; return NULL; }
const t5_storage_api_v1 *t5_storage_get_api(uint32_t version) { (void)version; return NULL; }
const t5_system_api_v1 *t5_system_get_api(uint32_t version) { (void)version; return NULL; }
const t5_system_ui_api_v1 *t5_system_ui_get_api(uint32_t version) { (void)version; return NULL; }
const t5_time_zone_api_v1 *t5_time_zone_get_api(uint32_t version) { (void)version; return NULL; }
const t5_ui_api_v1 *t5_ui_get_api(uint32_t version) { (void)version; return NULL; }
const t5_usb_api_v1 *t5_usb_get_api(uint32_t version) { (void)version; return NULL; }
const t5_web_server_api_v1 *t5_web_server_get_api(uint32_t version) { (void)version; return NULL; }
const t5_stream_api_v1 *t5_stream_get_api(uint32_t version) { (void)version; return NULL; }
