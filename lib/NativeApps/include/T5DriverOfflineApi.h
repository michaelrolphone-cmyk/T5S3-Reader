#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "T5DriverManagerApi.h"
#ifdef __cplusplus
extern "C" {
#endif
#define T5_DRIVER_OFFLINE_API_VERSION 1u
#define T5_DRIVER_LOCAL_INSTALLED 1u
#define T5_DRIVER_LOCAL_INBOX 2u
#define T5_DRIVER_LOCAL_VALID 1u
// An invalid package remains visible for diagnosis but may never be installed.
typedef struct {
    char id[T5_DRIVER_ID_MAX];
    char version[T5_DRIVER_VERSION_MAX];
    char capability[T5_DRIVER_CAPABILITY_MAX];
    uint32_t size_bytes;
    uint32_t source;
    uint32_t flags;
} t5_driver_local_entry_t;
typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    bool (*refresh)(void);
    uint32_t (*count)(void);
    bool (*get)(uint32_t index, t5_driver_local_entry_t *out);
    // Copy the validated inbox ELF to a disposable staging path and use the
    // canonical transactional installer. The inbox package remains intact.
    bool (*install)(uint32_t index);
} t5_driver_offline_api_v1;
const t5_driver_offline_api_v1 *t5_driver_offline_get_api(uint32_t version);
#ifdef __cplusplus
}
#endif
