#pragma once
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
// Blocking, task-context only. sd_path is an absolute /sd/... VFS path.
// Apps export void app_main(void), stop all work and return to exit.
// Native faults are not recoverable loader errors. See docs/NATIVE_APPS.md.
esp_err_t launch_elf_app(const char *sd_path);

// Optional module lifecycle exports. The loader calls app_module_init() after
// relocation and before any hardware takeover request, then calls
// app_module_fini() before restoring hardware and unmapping the ELF. C++ apps
// use these hooks to run linker-collected constructors and destructors.
typedef int (*elf_app_module_init_t)(void);
typedef void (*elf_app_module_fini_t)(void);

// Host-side helper used by reusable firmware services while app_main is active.
// Returns the exact /sd/... path passed to launch_elf_app(), or NULL otherwise.
// This symbol is not exported into the ELF application namespace.
const char *native_app_current_path(void);

// Read-only bridge to the existing, mutex-protected HalStorage/SdFat mount.
esp_err_t native_app_register_sd_vfs(void);
#ifdef __cplusplus
}
#endif
