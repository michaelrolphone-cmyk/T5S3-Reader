#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define T5_STORAGE_API_VERSION 1u

typedef uint32_t t5_storage_stream_t;
#define T5_STORAGE_STREAM_INVALID 0u

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;

    bool (*exists)(const char *path);
    bool (*read_file)(const char *path, void *buffer, size_t capacity, size_t *size_out);
    bool (*write_file_atomic)(const char *path, const void *data, size_t size);
    bool (*remove_file)(const char *path);

    /* Append-only extension: one read-only stream per native app session. */
    t5_storage_stream_t (*stream_open)(const char *path, size_t *size_out);
    size_t (*stream_read)(t5_storage_stream_t stream, void *buffer, size_t capacity);
    bool (*stream_seek)(t5_storage_stream_t stream, size_t offset);
    void (*stream_close)(t5_storage_stream_t stream);
} t5_storage_api_v1;

const t5_storage_api_v1 *t5_storage_get_api(uint32_t api_version);

#ifdef __cplusplus
}
#endif
