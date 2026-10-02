#pragma once
#include <stddef.h>
#include <stdint.h>
struct x4_embedded_provider {
    const char *id;
    const char *version;
    const char *capability;
    uint32_t api;
    const uint8_t *bytes;
    size_t size;
    uint8_t sha256[32];
    const char *const *imports;
    size_t import_count;
};
const x4_embedded_provider *x4_embedded_find(const char *id);
