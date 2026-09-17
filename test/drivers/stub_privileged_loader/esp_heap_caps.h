#pragma once
#include <stddef.h>
#include <stdint.h>
#define MALLOC_CAP_SPIRAM 0x400u
#define MALLOC_CAP_8BIT 0x004u
#ifdef __cplusplus
extern "C" {
#endif
void *heap_caps_malloc(size_t size, uint32_t capabilities);
void heap_caps_free(void *ptr);
#ifdef __cplusplus
}
#endif
