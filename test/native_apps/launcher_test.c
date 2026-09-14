#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "NativeAppLauncher.h"
#include "esp_dlfcn.h"
#include "esp_elf.h"
#include "T5AppApi.h"
#include "T5BatteryApi.h"
#include "T5FileBrowserApi.h"
#include "T5GpsApi.h"
#include "T5ImageApi.h"
#include "T5KOReaderApi.h"
#include "T5LoRaApi.h"
#include "T5NetworkApi.h"
#include "T5OpdsApi.h"
#include "T5StorageApi.h"
#include "T5SystemApi.h"
#include "T5SystemUiApi.h"
#include "T5UiApi.h"
#include "T5UsbApi.h"
#include "T5WebServerApi.h"

static int mode, opens, closes, calls, handle_storage;
static const char *pending;
static bool running;
const char *dlerror(void) { const char *e = pending; pending = NULL; return e; }
static void child(void)
{
    running = true;
    ++calls;
    assert(closes == 0);
    assert(native_app_current_path() != NULL);
    assert(strcmp(native_app_current_path(), "/sd/apps/game.elf") == 0);
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
    assert(handle == &handle_storage && strcmp(name, "app_main") == 0);
    if (mode == 2) { pending = "symbol missing"; return NULL; }
    if (mode == 3) return NULL;
    if (mode == 5) pending = "non-null symbol with loader error";
    return (void *)child;
}
int dlclose(void *handle)
{
    assert(handle == &handle_storage && !running);
    ++closes;
    if (mode == 4) { pending = "close failed"; return -1; }
    return 0;
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
        }
    }
    return 0;
}

esp_err_t native_app_register_sd_vfs(void) { return ESP_OK; }
int esp_elf_register_symbol(const struct esp_elfsym *s)
{
    assert(s && s[0].sym && s[1].sym && s[2].sym && s[3].sym && s[4].sym && s[5].sym && s[6].sym && s[7].sym && s[8].sym && s[9].sym && s[10].sym && s[11].sym && s[12].sym && s[13].sym && s[14].sym);
    const struct esp_elfsym *entry = s;
    while (entry->name && strcmp(entry->name, "snprintf") != 0) ++entry;
    assert(entry->name && entry->sym);
    int (*format)(char *, size_t, const char *, ...) = entry->sym;
    char text[8];
    assert(format(text, sizeof(text), "%s %d", "test", 12345) == 10);
    assert(strcmp(text, "test 12") == 0);
    return 0;
}
const t5_app_api_v1 *t5_app_get_api(uint32_t version) { (void)version; return NULL; }
const t5_battery_api_v1 *t5_battery_get_api(uint32_t version) { (void)version; return NULL; }
const t5_file_browser_api_v1 *t5_file_browser_get_api(uint32_t version) { (void)version; return NULL; }
const t5_gps_api_v1 *t5_gps_get_api(uint32_t version) { (void)version; return NULL; }
const t5_image_api_v1 *t5_image_get_api(uint32_t version) { (void)version; return NULL; }
const t5_koreader_api_v1 *t5_koreader_get_api(uint32_t version) { (void)version; return NULL; }
const t5_lora_api_v1 *t5_lora_get_api(uint32_t version) { (void)version; return NULL; }
const t5_network_api_v1 *t5_network_get_api(uint32_t version) { (void)version; return NULL; }
const t5_opds_api_v1 *t5_opds_get_api(uint32_t version) { (void)version; return NULL; }
const t5_storage_api_v1 *t5_storage_get_api(uint32_t version) { (void)version; return NULL; }
const t5_system_api_v1 *t5_system_get_api(uint32_t version) { (void)version; return NULL; }
const t5_system_ui_api_v1 *t5_system_ui_get_api(uint32_t version) { (void)version; return NULL; }
const t5_ui_api_v1 *t5_ui_get_api(uint32_t version) { (void)version; return NULL; }
const t5_usb_api_v1 *t5_usb_get_api(uint32_t version) { (void)version; return NULL; }
const t5_web_server_api_v1 *t5_web_server_get_api(uint32_t version) { (void)version; return NULL; }
