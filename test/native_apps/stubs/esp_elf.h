#pragma once
#include <stddef.h>
struct esp_elfsym { const char *name; const void *sym; };
#define ESP_ELFSYM_EXPORT(s) {#s, (void*)&s}
#define ESP_ELFSYM_END {NULL, NULL}
int esp_elf_register_symbol(const struct esp_elfsym *symbols);
