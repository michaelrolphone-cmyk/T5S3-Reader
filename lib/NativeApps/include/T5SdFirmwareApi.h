#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define T5_SD_FIRMWARE_API_VERSION 1u
#define T5_SD_FIRMWARE_PATH_MAX 256u

typedef enum {
    T5_SD_FIRMWARE_OK = 0,
    T5_SD_FIRMWARE_FILE_OPEN_FAILED = 1,
    T5_SD_FIRMWARE_TOO_LARGE = 2,
    T5_SD_FIRMWARE_TOO_SMALL = 3,
    T5_SD_FIRMWARE_INVALID = 4,
    T5_SD_FIRMWARE_WRITE_FAILED = 5,
    T5_SD_FIRMWARE_UNAVAILABLE = 255,
} t5_sd_firmware_result_t;

typedef void (*t5_sd_firmware_progress_callback_t)(void *ctx);

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;

    // The firmware wrapper owns file selection and exposes only the selected
    // firmware image to the active Settings ELF.
    bool (*selected_path)(char *buffer, size_t capacity);
    size_t (*image_size)(void);
    size_t (*written_size)(void);
    t5_sd_firmware_result_t (*validate)(void);
    t5_sd_firmware_result_t (*install)(t5_sd_firmware_progress_callback_t callback, void *ctx);

    // Narrow completion action; no generic reset primitive is exposed.
    void (*restart_after_update)(void);
} t5_sd_firmware_api_v1;

const t5_sd_firmware_api_v1 *t5_sd_firmware_get_api(uint32_t api_version);

#ifdef __cplusplus
}
#endif
