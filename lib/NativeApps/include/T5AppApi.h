#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define T5_APP_ABI_VERSION 1u
#define T5_APP_BUTTON_BACK (1u << 0)
#define T5_APP_BUTTON_CONFIRM (1u << 1)
#define T5_APP_BUTTON_LEFT (1u << 2)
#define T5_APP_BUTTON_RIGHT (1u << 3)
#define T5_APP_BUTTON_UP (1u << 4)
#define T5_APP_BUTTON_DOWN (1u << 5)
#define T5_APP_DIRENT_NAME_MAX 128u
#define T5_APP_ASSET_NAME_MAX 128u
#define T5_APP_VERSION_MAX 32u
#define T5_APP_SETTING_LABEL_MAX 128u
#define T5_APP_SETTING_VALUE_MAX 128u

typedef struct {
    uint32_t buttons;
    bool tapped;
    int16_t touch_x;
    int16_t touch_y;
    bool exit_requested; // Sticky after configured exit gestures.
} t5_app_input_t;

typedef struct {
    char name[T5_APP_DIRENT_NAME_MAX];
    uint64_t size;
    uint8_t is_directory;
} t5_app_dirent_t;

typedef struct {
    char name[T5_APP_ASSET_NAME_MAX];
    uint64_t size;
} t5_app_release_asset_t;

typedef enum {
    T5_APP_SETTING_TOGGLE = 0,
    T5_APP_SETTING_ENUM = 1,
    T5_APP_SETTING_ACTION = 2,
    T5_APP_SETTING_VALUE = 3,
    T5_APP_SETTING_STRING = 4,
    T5_APP_SETTING_TIMEZONE = 5,
} t5_app_setting_type_t;

typedef enum {
    T5_APP_SETTING_NO_CHANGE = 0,
    T5_APP_SETTING_UPDATED = 1,
    T5_APP_SETTING_ACTION_REQUESTED = 2,
    T5_APP_SETTING_ERROR = 3,
} t5_app_setting_result_t;

typedef struct {
    char label[T5_APP_SETTING_LABEL_MAX];
    char value[T5_APP_SETTING_VALUE_MAX];
    uint8_t type;
    uint8_t reserved[3];
} t5_app_setting_t;

typedef struct {
    char display_name[96];
    char file_name[128];
    char min_firmware_version[32];
    char icon[24]; // "solid:f013" or "regular:f007" (Classic Unicode codepoint).
    bool compatible;
} t5_app_manifest_t;

// Called synchronously on the App Store's owning native-app task while the
// firmware streams the selected release ELF. total_bytes is the catalog's
// verified size; callbacks and context are valid only for the download call.
typedef void (*t5_app_catalog_progress_fn)(void *context,
                                           uint64_t downloaded_bytes,
                                           uint64_t total_bytes);

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    int32_t (*screen_width)(void);
    int32_t (*screen_height)(void);
    void (*clear)(void);
    void (*draw_text)(int32_t x, int32_t y, const char *text);
    void (*fill_rect)(int32_t x, int32_t y, int32_t w, int32_t h, bool black);
    void (*present)(bool full_refresh);
    // Poll at least every 20-50ms. Updates input, yields, and feeds the watchdog.
    // Returns false outside the application's owning task.
    bool (*poll)(t5_app_input_t *input, uint32_t wait_ms);
    uint32_t (*millis)(void);
    // Read-only SD directory enumeration. Paths use the native VFS namespace,
    // e.g. "/sd" or "/sd/books". One directory may be open per app session.
    bool (*dir_open)(const char *path);
    bool (*dir_next)(t5_app_dirent_t *entry);
    void (*dir_close)(void);

    // Firmware-owned native-app catalog. Refresh connects with credentials already
    // saved by the firmware, queries this repository's latest GitHub release, and
    // caches only .elf assets. Downloads are constrained to /sd/Apps/<asset-name>.
    bool (*app_catalog_refresh)(void);
    uint32_t (*app_catalog_count)(void);
    bool (*app_catalog_get)(uint32_t index, t5_app_release_asset_t *asset);
    bool (*app_catalog_download)(uint32_t index);

    // Append-only native UI/settings bridge. Existing apps continue to see Back as
    // an exit gesture unless they explicitly disable it for in-app navigation.
    void (*set_back_exits_app)(bool enabled);

    // Settings metadata comes from the same firmware model used by SettingsActivity.
    // Category indexes are stable for the device UI: Display, Reader, Controls, System.
    uint32_t (*settings_category_count)(void);
    bool (*settings_category_get)(uint32_t category, char *label, size_t capacity);
    uint32_t (*settings_count)(uint32_t category);
    bool (*settings_get)(uint32_t category, uint32_t index, t5_app_setting_t *setting);
    uint8_t (*settings_activate)(uint32_t category, uint32_t index);

    // Render and touch-hit-test the native settings page through the active firmware
    // theme. This intentionally keeps theme metrics/font rendering inside firmware
    // while the ELF owns navigation and setting activation.
    void (*settings_render)(uint32_t category, int32_t selected_index);
    uint8_t (*settings_touch)(int16_t x, int16_t y, uint32_t *category, int32_t *selected_index);
    // Append-only springboard services. Check struct_size before accessing.
    bool (*installed_apps_refresh)(void);
    uint32_t (*installed_apps_count)(void);
    bool (*installed_apps_get)(uint32_t index, t5_app_manifest_t *manifest);
    // Queues an app; caller must return from app_main to unload before launch.
    bool (*request_app_launch)(uint32_t index);
    bool (*draw_icon)(int32_t x, int32_t y, const char *icon, uint8_t point_size, bool black);
    void (*draw_label)(int32_t x, int32_t y, int32_t width, const char *text);

    // Append-only release-catalog metadata. app_catalog_refresh validates and caches
    // each ELF's matching release manifest before exposing it through this getter.
    // Older firmware may not provide this member; callers must check struct_size.
    bool (*app_catalog_manifest_get)(uint32_t index, t5_app_manifest_t *manifest);

    // Append-only app version metadata. Versions are major.minor.patch strings from
    // each app's JSON sidecar. Legacy installed manifests return an empty string.
    // installed_app_version_get returns false only when that app is not installed or
    // its sidecar is invalid. Callers must check struct_size before accessing these.
    bool (*installed_app_version_get)(const char *file_name, char *version, size_t capacity);
    bool (*app_catalog_version_get)(uint32_t index, char *version, size_t capacity);
    // Present a fast frame while periodically calling service on the app owner
    // task. The callback may collect provider input, but MUST NOT draw, call UI
    // APIs, launch/exit an app, or block indefinitely. Framebuffer remains frozen.
    // Returns only after display completion; no callback/framebuffer access outlives this call.
    // False means no refresh started (busy or allocation failure). Size-check.
    bool (*present_serviced)(bool full_refresh, void (*service)(void *), void *context);
    // App Store failure detail for display when serial logging is unavailable.
    // Optional append-only member; callers must check struct_size and pointer.
    bool (*app_catalog_download_last_error)(char *out, size_t capacity);
    // Optional append-only variant that reports synchronous ELF download progress.
    bool (*app_catalog_download_with_progress)(uint32_t index,
                                               t5_app_catalog_progress_fn progress,
                                               void *context);

    // Append-only bulk resident memory service. Large app working sets belong
    // in PSRAM so internal SRAM remains available for TLS, stacks, DMA and
    // hardware-facing allocations. This never falls back to internal RAM.
    void *(*psram_alloc)(size_t size);
    void (*psram_free)(void *ptr);
} t5_app_api_v1;

// Native application entry point. Native ELFs are built with -fvisibility=hidden,
// so the ABI header explicitly keeps app_main discoverable through dlsym().
__attribute__((visibility("default"))) void app_main(void);

// This is the single versioned firmware symbol imported by native UI apps.
// NULL means unsupported ABI or no active UI session. Do not call from workers.
const t5_app_api_v1 *t5_app_get_api(uint32_t abi_version);
#ifdef __cplusplus
}
#endif
