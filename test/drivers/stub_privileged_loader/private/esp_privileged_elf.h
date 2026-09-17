#pragma once
#include <stddef.h>
#include <stdint.h>
#include <esp_elf.h>
#ifdef __cplusplus
extern "C" {
#endif
int esp_elf_relocate_privileged_verified_v1(esp_elf_t *module,
                                            const uint8_t *image,
                                            size_t length,
                                            const char *const *signed_imports,
                                            size_t signed_import_count);
#ifdef __cplusplus
}
#endif
