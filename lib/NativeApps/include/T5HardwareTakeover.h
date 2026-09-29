#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Optional, ELF-defined entry point. The loader looks it up before app_main.
 * An ELF without this symbol runs under the normal firmware UI/display owner.
 * Export with default visibility; no firmware-side symbol import is necessary.
 *
 * Example:
 *   __attribute__((visibility("default")))
 *   uint32_t app_hardware_takeover(void) {
 *       return T5_HARDWARE_TAKEOVER_DISPLAY;
 *   }
 *
 * The loader relinquishes only the requested resources, calls app_main, and
 * restores firmware ownership after app_main RETURNS, before dlclose. The ELF
 * must stop all hardware tasks, interrupts, callbacks and DMA before returning.
 * Physical refresh/UI dialogs are forbidden during takeover. UI_VIDEO permits
 * software drawing primitives and copy_ui_frame(), never present().
 */
#define T5_HARDWARE_TAKEOVER_DISPLAY (1u << 0)
// UI video keeps firmware touch capture and software rasterization available.
// It must be combined with DISPLAY; physical present/UI dialogs stay forbidden.
#define T5_HARDWARE_TAKEOVER_UI_VIDEO (1u << 1)
#define T5_HARDWARE_TAKEOVER_SUPPORTED (T5_HARDWARE_TAKEOVER_DISPLAY | T5_HARDWARE_TAKEOVER_UI_VIDEO)

typedef uint32_t (*t5_hardware_takeover_request_fn)(void);

#ifdef __cplusplus
}
#endif
