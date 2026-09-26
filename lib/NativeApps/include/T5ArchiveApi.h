#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define T5_ARCHIVE_API_VERSION 1u
typedef bool (*t5_archive_progress_fn)(void *context, uint64_t completed, uint64_t total);
typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    bool (*find_first_suffix)(const char *zip_path, const char *suffix,
                              char *entry_name, size_t entry_capacity,
                              uint64_t *uncompressed_size);
    bool (*extract_file)(const char *zip_path, const char *entry_name,
                         const char *destination_path, uint64_t max_uncompressed_size,
                         uint32_t timeout_ms, t5_archive_progress_fn progress,
                         void *progress_context);
} t5_archive_api_v1;
const t5_archive_api_v1 *t5_archive_get_api(uint32_t api_version);
#ifdef __cplusplus
}
#endif
