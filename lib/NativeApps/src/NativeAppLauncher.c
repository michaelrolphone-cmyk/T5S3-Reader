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
#include "T5LocationApi.h"
#include "T5LoRaApi.h"
#include "T5NetworkApi.h"
#include "T5OpdsApi.h"
#include "T5OtaApi.h"
#include "T5PackageManagerApi.h"
#include "T5ProgramEspRomApi.h"
#include "T5ProviderCapabilityApi.h"
#include "T5SdFirmwareApi.h"
#include "T5SerialPortApi.h"
#include "T5StatusBarApi.h"
#include "T5StorageApi.h"
#include "T5StreamApi.h"
#include "T5SystemApi.h"
#include "T5SystemUiApi.h"
#include "T5TimeZoneApi.h"
#include "T5UiApi.h"
#include "T5UsbApi.h"
#include "T5WebServerApi.h"
#include <errno.h>

#if !CONFIG_IDF_TARGET_ESP32S3 || !CONFIG_ELF_LOADER_LOAD_PSRAM
#error "T5S3 native apps require the S3 PSRAM loader configuration"
#endif

// Firmware-only owner-task hooks; never exported to application ELFs.
extern bool native_app_capabilities_ready(const char *sd_path);
extern bool native_app_capabilities_bind(const char *sd_path);
extern void native_app_capabilities_release(void);
extern void native_app_provider_capabilities_release(void);
// All physical handoff is performed by the host while NativeAppHost holds
// RenderLock. Individual ELFs only declare their requested resource mask.
extern esp_err_t native_hardware_takeover_begin(uint32_t requested);
extern esp_err_t native_hardware_takeover_end(uint32_t requested);
// Temporary direct hardware import inventory, registered only for the
// lifetime of the current ELF. Neither function is an ELF export.
extern int native_hardware_compat_register(void);
extern void native_hardware_compat_unregister(void);

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
    bool compat_registered = false;
    bool module_initialized = false;
    bool takeover_active = false;
    elf_app_module_fini_t module_fini = NULL;
    esp_err_t result = native_app_register_sd_vfs();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "SD VFS unavailable: %s", esp_err_to_name(result));
        goto done;
    }
    if (!native_app_capabilities_ready(sd_path) || !native_app_capabilities_bind(sd_path)) {
        ESP_LOGE(TAG, "Required application dependencies unavailable or could not be bound: %s", sd_path);
        result = ESP_ERR_NOT_SUPPORTED;
        goto done;
    }
    static const struct esp_elfsym host_symbols[] = {
        ESP_ELFSYM_EXPORT(t5_app_get_api),
        ESP_ELFSYM_EXPORT(t5_battery_get_api),
        ESP_ELFSYM_EXPORT(t5_button_remap_get_api),
        ESP_ELFSYM_EXPORT(t5_cache_get_api),
        ESP_ELFSYM_EXPORT(t5_device_get_api),
        ESP_ELFSYM_EXPORT(t5_driver_manager_get_api),
        ESP_ELFSYM_EXPORT(t5_package_manager_get_api),
        ESP_ELFSYM_EXPORT(t5_font_get_api),
        ESP_ELFSYM_EXPORT(t5_koreader_get_api),
        ESP_ELFSYM_EXPORT(t5_language_get_api),
        ESP_ELFSYM_EXPORT(t5_location_get_api),
        ESP_ELFSYM_EXPORT(t5_opds_get_api),
        ESP_ELFSYM_EXPORT(t5_ota_get_api),
        ESP_ELFSYM_EXPORT(t5_program_esp_rom_get_api),
        ESP_ELFSYM_EXPORT(t5_provider_capability_get_api),
        ESP_ELFSYM_EXPORT(t5_sd_firmware_get_api),
        ESP_ELFSYM_EXPORT(t5_serial_port_get_api),
        ESP_ELFSYM_EXPORT(t5_status_bar_get_api),
        ESP_ELFSYM_EXPORT(t5_storage_get_api),
        ESP_ELFSYM_EXPORT(t5_stream_get_api),
        ESP_ELFSYM_EXPORT(t5_system_get_api),
        ESP_ELFSYM_EXPORT(t5_system_ui_get_api),
        ESP_ELFSYM_EXPORT(t5_time_zone_get_api),
        ESP_ELFSYM_EXPORT(t5_ui_get_api),
        ESP_ELFSYM_EXPORT(t5_network_get_api),
        ESP_ELFSYM_EXPORT(t5_file_browser_get_api),
        ESP_ELFSYM_EXPORT(t5_file_open_get_api),
        ESP_ELFSYM_EXPORT(t5_image_get_api),
        ESP_ELFSYM_EXPORT(t5_gps_get_api),
        ESP_ELFSYM_EXPORT(t5_lora_get_api),
        ESP_ELFSYM_EXPORT(t5_web_server_get_api),
        ESP_ELFSYM_EXPORT(t5_usb_get_api),
        ESP_ELFSYM_END
    };
    const int registered = esp_elf_register_symbol(host_symbols);
    if (registered != 0 && registered != -EEXIST) {
        result = ESP_ERR_NO_MEM;
        ESP_LOGE(TAG, "Could not register native app APIs");
        goto done;
    }
    // These direct imports are temporary; normal RiscRTE API exports remain
    // registered, and provider relocation still uses its isolated resolver.
    const int compat_rc = native_hardware_compat_register();
    if (compat_rc != 0) {
        result = ESP_ERR_NO_MEM;
        ESP_LOGE(TAG, "Could not register native hardware compatibility symbols: %d", compat_rc);
        goto done;
    }
    compat_registered = true;
    result = ESP_FAIL;
    (void)dlerror();
    ESP_LOGI(TAG, "Loading %s", sd_path);
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

    // dlopen relocates an ESP ELF but does not execute C++ constructor tables.
    // Apps that contain nontrivial static objects expose bounded lifecycle
    // hooks; keep legacy C apps compatible by treating both symbols as optional.
    (void)dlerror();
    void *init_symbol = dlsym(handle, "app_module_init");
    const char *init_error = dlerror();
    const bool init_present = init_error == NULL && init_symbol != NULL;
    (void)dlerror();
    void *fini_symbol = dlsym(handle, "app_module_fini");
    const char *fini_error = dlerror();
    const bool fini_present = fini_error == NULL && fini_symbol != NULL;
    if (init_present != fini_present) {
        ESP_LOGE(TAG, "Incomplete module lifecycle exports in %s", sd_path);
        result = ESP_ERR_INVALID_STATE;
        goto close_module;
    }
    if (init_present) {
        ESP_LOGI(TAG, "Module init %s at %p", sd_path, init_symbol);
        module_fini = (elf_app_module_fini_t)fini_symbol;
        if (((elf_app_module_init_t)init_symbol)() != 0) {
            ESP_LOGE(TAG, "Module initialization failed for %s", sd_path);
            result = ESP_ERR_INVALID_STATE;
            goto close_module;
        }
        module_initialized = true;
    }

    // Opt-in, generic ABI: missing symbol means the legacy UI/display path.
    // Query the mask only after mapping succeeds; do not release the panel for
    // a corrupt ELF, a missing app_main, or a module that does not request it.
    uint32_t requested = 0U;
    (void)dlerror();
    void *request_symbol = dlsym(handle, "app_hardware_takeover");
    const char *request_error = dlerror();
    if (request_error == NULL && request_symbol != NULL) {
        requested = ((t5_hardware_takeover_request_fn)request_symbol)();
    }
    if (requested != 0U) {
        ESP_LOGI(TAG, "Hardware takeover %s mask=0x%08lx", sd_path, (unsigned long)requested);
        result = native_hardware_takeover_begin(requested);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "Hardware takeover denied for %s, mask=0x%08lx: %s",
                     sd_path, (unsigned long)requested, esp_err_to_name(result));
            goto close_module;
        }
        takeover_active = true;
    }

    ESP_LOGI(TAG, "Starting %s app_main=%p", sd_path, symbol);
    s_current_path = sd_path;
    ((elf_app_main_t)symbol)();
    s_current_path = NULL;
    ESP_LOGI(TAG, "Application returned: %s", sd_path);
    result = ESP_OK;

close_module:
    s_current_path = NULL;
    // Destructors may release app-owned peripherals and callbacks, so run them
    // while the module is mapped and before the host restores shared hardware.
    if (module_initialized && module_fini != NULL) {
        module_fini();
        module_initialized = false;
    }
    // Never unload a module with an outstanding provider grant. This owner-task
    // cleanup also runs on early exits and is idempotent.
    native_app_provider_capabilities_release();
    // An ELF MUST stop all of its hardware tasks/IRQs/DMA before returning.
    // Restore before dlclose so the app and its callbacks cannot reference
    // unmapped code after host hardware is reinitialized.
    if (takeover_active) {
        esp_err_t restore = native_hardware_takeover_end(requested);
        if (restore != ESP_OK) {
            ESP_LOGE(TAG, "Failed to restore host hardware for %s: %s",
                     sd_path, esp_err_to_name(restore));
            result = restore;
        }
        takeover_active = false;
    }
    native_app_capabilities_release();
    (void)dlerror();
    if (dlclose(handle) != 0) {
        const char *close_error = dlerror();
        ESP_LOGE(TAG, "dlclose(%s): %s", sd_path,
                 close_error != NULL ? error : "unload failed without a diagnostic");
        result = ESP_FAIL;
    }
done:
    native_app_provider_capabilities_release();
    if (compat_registered) native_hardware_compat_unregister();
    native_app_capabilities_release();
    s_current_path = NULL;
    atomic_flag_clear_explicit(&s_running, memory_order_release);
    return result;
}
