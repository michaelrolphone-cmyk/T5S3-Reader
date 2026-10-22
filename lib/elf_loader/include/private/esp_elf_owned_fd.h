/* Internal selected-volume loader entry points. Not an application ABI/export. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_elf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*esp_elf_snapshot_admit_fn)(void *context, const uint8_t *bytes, size_t size);

/* Consume fd exactly once, including failure and invalid-argument paths. Never
 * retry close: ESP-IDF discards the global descriptor even if close fails.
 * Admission is mandatory and sees the owned copy only after checked close.
 * On failure, file is unchanged. On success, esp_elf_close releases its copy. */
int esp_elf_open_owned_fd(elf_file_t *file, int fd, esp_elf_snapshot_admit_fn admit, void *context);

/* name is a module label, never a path to reopen. Existing basename modules
 * cause failure, including a collision discovered after relocation. */
void *esp_dlopen_owned_fd(const char *name, int fd, esp_elf_snapshot_admit_fn admit, void *context);

#ifdef __cplusplus
}
#endif
