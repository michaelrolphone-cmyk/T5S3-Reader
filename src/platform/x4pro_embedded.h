#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct x4_embedded_provider {
    const char *id;
    const char *version;
    const char *capability;
    uint32_t api;
    const uint8_t *bytes;
    size_t size;
    uint8_t sha256[32];
    const char *const *imports;
    size_t import_count;
} x4_embedded_provider;
const x4_embedded_provider *x4_embedded_find(const char *id);
#ifdef __cplusplus
}
#endif
