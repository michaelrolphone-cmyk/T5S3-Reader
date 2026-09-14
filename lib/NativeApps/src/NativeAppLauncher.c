#include "NativeAppLauncher.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_dlfcn.h"
#include "esp_log.h"

#include "esp_elf.h"
#include "T5AppApi.h"
#include "T5BatteryApi.h"
#include "T5CacheApi.h"
#include "T5FileBrowserApi.h"
#include "T5GpsApi.h"
#include "T5ImageApi.h"
#include "T5KOReaderApi.h"
#include "T5LoRaApi.h"
#include "T5NetworkApi.h"
#include "T5OpdsApi.h"
#include "T5OtaApi.h"
#include "T5StorageApi.h"
#include "T5SystemApi.h"
#include "T5SystemUiApi.h"
#include "T5UiApi.h"
#include "T5UsbApi.h"
#include "T5WebServerApi.h"
#include <errno.h>

#if !CONFIG_IDF_TARGET_ESP32S3 || !CONFIG_ELF_LOADER_LOAD_PSRAM
#error "T5S3 native apps require the S3 PSRAM loader configuration"
#endif

static const char *TAG = "sd_elf_launcher";
static atomic_flag s_running = ATOMIC_FLAG_INIT;
static const char *s_current_path = NULL;
typedef void (*elf_app_main_t)(void);

const char *native_app_current_path(void)
{
    return s_current_path;
}

esp_err_t launch_elf_app(const char *sd_path)
{
    if (sd_path == NULL || strncmp(sd_path, "/sd/", 4) != 0 || sd_path[4] == '\0') {
        ESP_LOGE(TAG, "Expected an absolute SD VFS file path");
        return ESP_ERR_INVALID_ARG;
    }
    if (atomic_flag_test_and_set_explicit(&s_running, memory_order_acquire)) {
        ESP_LOGE(TAG, "An ELF application is already running");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = native_app_register_sd_vfs();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "SD VFS unavailable: %s", esp_err_to_name(result));
        goto done;
    }
    static const struct esp_elfsym host_symbols[] = {
        ESP_ELFSYM_EXPORT(t5_app_get_api),
        ESP_ELFSYM_EXPORT(t5_battery_get_api),
        ESP_ELFSYM_EXPORT(t5_cache_get_api),
        ESP_ELFSYM_EXPORT(t5_koreader_get_api),
        ESP_ELFSYM_EXPORT(t5_opds_get_api),
        ESP_ELFSYM_EXPORT(t5_ota_get_api),
        ESP_ELFSYM_EXPORT(t5_storage_get_api),
        ESP_ELFSYM_EXPORT(t5_system_get_api),
        ESP_ELFSYM_EXPORT(t5_system_ui_get_api),
        ESP_ELFSYM_EXPORT(t5_ui_get_api),
        ESP_ELFSYM_EXPORT(t5_network_get_api),
        ESP_ELFSYM_EXPORT(t5_file_browser_get_api),
        ESP_ELFSYM_EXPORT(t5_image_get_api),
        ESP_ELFSYM_EXPORT(t5_gps_get_api),
        ESP_ELFSYM_EXPORT(t5_lora_get_api),
        ESP_ELFSYM_EXPORT(t5_web_server_get_api),
        ESP_ELFSYM_EXPORT(t5_usb_get_api),
        // Native apps format bounded status/error text. The vendored loader's
        // default libc table exports printf, but does not export snprintf.
        ESP_ELFSYM_EXPORT(snprintf),
        ESP_ELFSYM_END
    };
    const int registered = esp_elf_register_symbol(host_symbols);
    if (registered != 0 && registered != -EEXIST) {
        result = ESP_ERR_NO_MEM;
        ESP_LOGE(TAG, "Could not register native app APIs");
        goto done;
    }
    result = ESP_FAIL;
    (void)dlerror();
    void *handle = dlopen(sd_path, RTLD_NOW);
    if (handle == NULL) {
        const char *error = dlerror();
        ESP_LOGE(TAG, "dlopen(%s): %s", sd_path,
                 error != NULL ? error : "loader returned NULL without a diagnostic");
        goto done;
    }

    (void)dlerror();
    void *symbol = dlsym(handle, "app_main");
    const char *error = dlerror();
    if (error != NULL || symbol == NULL) {
        ESP_LOGE(TAG, "dlsym(app_main) in %s: %s", sd_path,
                 error != NULL ? error : "entry point has a NULL address");
        result = ESP_ERR_NOT_FOUND;
        goto close_module;
    }

    ESP_LOGI(TAG, "Starting %s", sd_path);
    s_current_path = sd_path;
    ((elf_app_main_t)symbol)();
    s_current_path = NULL;
    ESP_LOGI(TAG, "Application returned: %s", sd_path);
    result = ESP_OK;

close_module:
    s_current_path = NULL;
    (void)dlerror();
    if (dlclose(handle) != 0) {
        const char *close_error = dlerror();
        ESP_LOGE(TAG, "dlclose(%s): %s", sd_path,
                 close_error != NULL ? close_error : "unload failed without a diagnostic");
        result = ESP_FAIL;
    }
done:
    s_current_path = NULL;
    atomic_flag_clear_explicit(&s_running, memory_order_release);
    return result;
}
