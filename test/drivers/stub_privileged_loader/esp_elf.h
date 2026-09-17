#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct esp_symtab {
    void *addr;
    char *name;
} esp_symtab_t;
typedef struct esp_elf {
    uint16_t num;
    esp_symtab_t *symtab;
} esp_elf_t;
int esp_elf_init(esp_elf_t *module);
int esp_elf_relocate(esp_elf_t *module, const uint8_t *image);
void esp_elf_deinit(esp_elf_t *module);
#ifdef __cplusplus
}
#endif
