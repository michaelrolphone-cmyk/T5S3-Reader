#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define T5_OTA_API_VERSION 1u
#define T5_OTA_VERSION_MAX 32u

typedef enum {
    T5_OTA_OK = 0,
    T5_OTA_NO_UPDATE = 1,
    T5_OTA_HTTP_ERROR = 2,
    T5_OTA_JSON_PARSE_ERROR = 3,
    T5_OTA_UPDATE_OLDER_ERROR = 4,
    T5_OTA_INTERNAL_UPDATE_ERROR = 5,
    T5_OTA_OOM_ERROR = 6,
    T5_OTA_UNAVAILABLE = 255,
} t5_ota_result_t;

typedef void (*t5_ota_progress_callback_t)(void *ctx);

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;

    // Network selection remains firmware-owned. These calls are available only
    // while a native app is active and WiFi has already been connected by the
    // firmware wrapper activity.
    t5_ota_result_t (*check_for_update)(void);
    bool (*is_update_newer)(void);
    bool (*latest_version)(char *buffer, size_t buffer_size);
    size_t (*processed_size)(void);
    size_t (*total_size)(void);
    t5_ota_result_t (*install_update)(t5_ota_progress_callback_t callback, void *ctx);

    // Explicit OTA completion action; native apps do not receive a generic
    // reset primitive through this API.
    void (*restart_after_update)(void);
} t5_ota_api_v1;

const t5_ota_api_v1 *t5_ota_get_api(uint32_t api_version);

#ifdef __cplusplus
}
#endif
