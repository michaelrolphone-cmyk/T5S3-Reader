#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define T5_STORAGE_API_VERSION 1u

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;

    // Native paths use the same /sd/... namespace as the ELF loader. The host
    // maps them onto the firmware's existing HalStorage mount.
    bool (*exists)(const char *path);

    // Reads an entire file. `size_out` receives the full byte count. When buffer
    // is NULL or capacity is zero this is a size query only. Returns false when
    // the file does not exist or cannot be read.
    bool (*read_file)(const char *path, void *buffer, size_t capacity, size_t *size_out);

    // Atomically-ish replace a file by writing a sibling .part file and renaming
    // it into place only after the complete payload is on the SD card. Parent
    // directories are created by the firmware storage layer when needed.
    bool (*write_file_atomic)(const char *path, const void *data, size_t size);

    bool (*remove_file)(const char *path);
} t5_storage_api_v1;

const t5_storage_api_v1 *t5_storage_get_api(uint32_t api_version);

#ifdef __cplusplus
}
#endif
