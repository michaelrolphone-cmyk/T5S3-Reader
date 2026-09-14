#pragma once
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
// Blocking, task-context only. sd_path is an absolute /sd/... VFS path.
// Apps export void app_main(void), stop all work and return to exit.
// Native faults are not recoverable loader errors. See docs/NATIVE_APPS.md.
esp_err_t launch_elf_app(const char *sd_path);

// Host-side helper used by reusable firmware services while app_main is active.
// Returns the exact /sd/... path passed to launch_elf_app(), or NULL otherwise.
// This symbol is not exported into the ELF application namespace.
const char *native_app_current_path(void);

// Read-only bridge to the existing, mutex-protected HalStorage/SdFat mount.
esp_err_t native_app_register_sd_vfs(void);
#ifdef __cplusplus
}
#endif
