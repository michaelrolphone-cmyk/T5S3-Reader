#pragma once
#include "RiscDisplayOutputV1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Optional display.output@1 suffix, after the exact existing history prefix.
 * This is provider preparation, NOT system sleep entry or permission to unload.
 * Both calls run on the provider's serialized owner and are bounded by the
 * supplied cooperative budget (including admission), capped at 1500 ms.
 * Providers document their finite indivisible command overshoot. Zero is a
 * non-mutating poll: OK only if the requested state is already established.
 * No frame may be acquired, queued or active when preparing. While partially
 * or fully prepared, normal frame/present operations must fail closed.
 * prepare preserves the visible image and the provider's owned resources.
 * A non-retained error may leave partial preparation; call resume before use.
 * resume is idempotent, reinitializes without a visible refresh, and restores
 * ordinary use after preparation or ordinary refusal. It invalidates previous
 * image seeding. Fresh MCU boot is separate: reload/init the provider, recreate
 * the old visible image, seed it, then draw/submit current pixels; use an honest
 * full refresh when the previous image cannot be reconstructed.
 * RETAINED means uncertain I/O/cleanup: keep code, configuration, dependencies
 * and resources pinned; do not retry ordinary operations or claim recovery.
 */
#define RISC_DISPLAY_POWER_TAG 0x44505731u /* DPW1 */
#define RISC_DISPLAY_POWER_MAX_BUDGET_MS 1500u
enum {
    RISC_DISPLAY_POWER_OK = 0,
    RISC_DISPLAY_POWER_BUSY = -1,
    RISC_DISPLAY_POWER_TIMEOUT = -2,
    RISC_DISPLAY_POWER_UNAVAILABLE = -3,
    RISC_DISPLAY_POWER_RETAINED = -4,
    RISC_DISPLAY_POWER_PLATFORM = -5
};
typedef struct {
    risc_display_output_api_v1_history history;
    uint32_t power_tag, power_version;
    int32_t (*prepare)(void *context, uint32_t timeout_ms);
    int32_t (*resume)(void *context, uint32_t timeout_ms);
} risc_display_output_api_v1_power;

static inline const risc_display_output_api_v1_power *risc_display_output_power(
    const risc_display_output_api_v1 *api) {
    if (!risc_display_output_history(api) ||
        api->struct_size < sizeof(risc_display_output_api_v1_power)) return NULL;
    const risc_display_output_api_v1_power *ext = (const risc_display_output_api_v1_power *)api;
    return ext->power_tag == RISC_DISPLAY_POWER_TAG && ext->power_version == 1u &&
        ext->prepare && ext->resume ? ext : NULL;
}
#ifdef __cplusplus
}
#endif
