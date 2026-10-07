#pragma once
#include "RiscTouchV1.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Optional input.touch.raw@1 suffix. The original RiscTouchV1 prefix and
 * capability identity are unchanged; size alone never identifies this API.
 * These owner-executor operations prepare a LOADED provider, not system sleep
 * entry or permission to unload. Keep its grant, code and dependencies alive.
 * prepare refuses every live subscription without changing the input stream.
 * Once preparation begins, ordinary subscribe/poll/next/snapshot calls are
 * fenced until resume succeeds. Exact resource custody survives partial work.
 * A non-retained error may leave preparation incomplete: retry prepare, or
 * resume to recover ordinary input. resume is idempotent and may complete
 * partial preparation before recovery. It never resurrects old subscriptions.
 * Recovery discards stale events and hides inherited contacts AND buttons
 * until a fresh, valid, acknowledged all-neutral report. Consumers establish
 * a new subscription and snapshot; no held input becomes an activation edge.
 *
 * timeout_ms is one cooperative budget, including admission and all transfers,
 * capped below. Zero is a non-mutating state poll: OK only when the requested
 * state is already established, BUSY otherwise. Positive work is finite; no
 * unbounded polling/retries. Providers document fixed cleanup/scheduler
 * overshoot; an expired/non-monotonic clock cannot produce success. A timeout
 * retains the precise partial stage and tokens, never claims sleep readiness.
 * PLATFORM indicates an ordinary recoverable refusal, with confirmed custody.
 * RETAINED means uncertain I/O/cleanup: keep every resource and dependency
 * pinned; no further hardware operation/retry or successful unload is allowed.
 * Fresh MCU boot is separate and starts a new provider through its normal API.
 */
#define RISC_TOUCH_POWER_TAG 0x54505731u /* TPW1 */
#define RISC_TOUCH_POWER_MAX_BUDGET_MS 1000u
enum {
    RISC_TOUCH_POWER_OK = 0,
    RISC_TOUCH_POWER_BUSY = -1,
    RISC_TOUCH_POWER_TIMEOUT = -2,
    RISC_TOUCH_POWER_UNAVAILABLE = -3,
    RISC_TOUCH_POWER_RETAINED = -4,
    RISC_TOUCH_POWER_PLATFORM = -5,
    RISC_TOUCH_POWER_INVALID = -6
};
typedef struct {
    risc_touch_api_v1 base;
    uint32_t power_tag, power_version;
    int32_t (*prepare)(void *context, uint32_t timeout_ms);
    int32_t (*resume)(void *context, uint32_t timeout_ms);
} risc_touch_power_api_v1;

static inline const risc_touch_power_api_v1 *risc_touch_power(const risc_touch_api_v1 *api) {
    if (!api || api->api_version != RISC_TOUCH_API_V1 ||
        api->struct_size < sizeof(risc_touch_power_api_v1)) return NULL;
    const risc_touch_power_api_v1 *power = (const risc_touch_power_api_v1 *)api;
    return power->power_tag == RISC_TOUCH_POWER_TAG && power->power_version == 1u &&
        api->subscribe && api->unsubscribe && api->poll && api->next && api->snapshot &&
        power->prepare && power->resume ? power : NULL;
}
#ifdef __cplusplus
}
#endif
