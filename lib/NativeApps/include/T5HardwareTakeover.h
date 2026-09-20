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
 * It must not call the firmware's normal display/UI API during takeover.
 */
#define T5_HARDWARE_TAKEOVER_DISPLAY (1u << 0)
#define T5_HARDWARE_TAKEOVER_SUPPORTED (T5_HARDWARE_TAKEOVER_DISPLAY)

typedef uint32_t (*t5_hardware_takeover_request_fn)(void);

#ifdef __cplusplus
}
#endif
