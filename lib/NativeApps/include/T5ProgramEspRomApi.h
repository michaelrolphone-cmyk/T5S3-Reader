#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "T5StreamApi.h"
#ifdef __cplusplus
extern "C" {
#endif

#define T5_PROGRAM_ESP_ROM_API_VERSION 1u
#define T5_PROGRAM_ESP_ROM_CAPABILITY "program.esp_rom"
#define T5_PROGRAM_ESP_ROM_MESSAGE_MAX 160u

typedef enum {
    T5_PROGRAM_STAGE_VALIDATE = 1,
    T5_PROGRAM_STAGE_HASH,
    T5_PROGRAM_STAGE_CONNECT,
    T5_PROGRAM_STAGE_CONFIGURE,
    T5_PROGRAM_STAGE_ERASE,
    T5_PROGRAM_STAGE_WRITE,
    T5_PROGRAM_STAGE_VERIFY,
    T5_PROGRAM_STAGE_RESET,
    T5_PROGRAM_STAGE_COMPLETE
} t5_program_esp_rom_stage_t;

typedef enum {
    T5_PROGRAM_OK = 0,
    T5_PROGRAM_INVALID = -1,
    T5_PROGRAM_DENIED = -2,
    T5_PROGRAM_UNSUPPORTED = -3,
    T5_PROGRAM_BUSY = -4,
    T5_PROGRAM_IO = -5,
    T5_PROGRAM_TARGET_LOST = -6,
    T5_PROGRAM_CANCELLED = -7,
    T5_PROGRAM_VERIFY_FAILED = -8,
    T5_PROGRAM_TIMEOUT = -9
} t5_program_esp_rom_result_t;

typedef struct {
    uint32_t struct_size;
    uint8_t stage;
    uint8_t percent;
    uint8_t rom_command;
    uint8_t rom_status;
    uint8_t rom_error;
    uint8_t reserved[3];
    uint32_t bytes_written;
    uint32_t image_bytes;
    int32_t result;
    char message[T5_PROGRAM_ESP_ROM_MESSAGE_MAX];
} t5_program_esp_rom_status_v1;

/* Called synchronously on the invoking app task. The runtime NEVER stores the
 * callback or context. Returning false cancels and releases the serial lease.
 * The callback may draw progress and poll the app's Back/Home controls. */
typedef bool (*t5_program_esp_rom_progress_fn)(void *context,
                                                const t5_program_esp_rom_status_v1 *status);

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    const char *capability_id;
    /* One bounded, context-owned programming operation. The borrowed stream
     * must be readable and seekable and remains owned/closed by the caller.
     * The provider acquires/releases serial.port and its two RX/TX streams.
     * A merged ESP firmware image at flash offset zero is required.
     * No ELF callback survives the call, even on error or cancellation. */
    t5_program_esp_rom_result_t (*program)(t5_stream_t firmware_stream,
        uint32_t image_bytes, t5_program_esp_rom_progress_fn progress,
        void *context, t5_program_esp_rom_status_v1 *final_status);
} t5_program_esp_rom_api_v1;

const t5_program_esp_rom_api_v1 *t5_program_esp_rom_get_api(uint32_t version);
#ifdef __cplusplus
}
#endif
