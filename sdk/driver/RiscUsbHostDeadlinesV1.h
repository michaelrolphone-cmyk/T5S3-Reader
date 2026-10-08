#pragma once
/* Optional USB provider-to-provider deadline support. This is not a firmware
 * import or a hardware grant. The deployed host snapshot prefix is unchanged. */
#include "RiscUsbControllerV1.h"
#include "RiscStreamResultV1.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_USB_HOST_DEADLINES_TAG_V1 UINT32_C(0x55484431)
#define RISC_USB_HOST_DEADLINES_API_V1 1u
typedef struct {
    uint32_t api_version, struct_size;
    /* Monotonic milliseconds; nonblocking and never reset while started. */
    uint64_t (*now_ms)(void *context);
    /* Every positive budget is TOTAL across admission, controller callbacks,
     * event processing and cleanup. No call may outlive it or retain buffers.
     * snapshot consumes at most 16 events and copies a complete unique set of
     * at most RISC_USB_HOST_MAX_DEVICES generation-qualified tokens. Unknown,
     * overflow or partial inventory returns IO, never an empty success. */
    int32_t (*snapshot)(void *, uint64_t *, size_t *, uint32_t budget_ms);
    int32_t (*configuration)(void *, uint64_t, uint8_t *, size_t *,
                             uint16_t *, uint16_t *, uint32_t budget_ms);
    /* OK yields a NEW nonzero claim. A clean error yields zero. RETAINED
     * reports the exact surviving claim, if known; never silently orphan it.
     * Callers must retain their dependency even when that claim is zero. */
    int32_t (*claim)(void *, uint64_t, uint8_t, uint8_t, uint64_t *, uint32_t);
    /* Only OK proves physical closure. A failure keeps claim custody and may
     * not be retried automatically after the caller fences the session. */
    int32_t (*release)(void *, uint64_t, uint32_t budget_ms);
} risc_usb_host_deadline_api_v1;
typedef struct {
    risc_usb_host_snapshot_v1 snapshot;
    uint32_t extension_tag, extension_version;
    const risc_usb_host_deadline_api_v1 *deadlines;
} risc_usb_host_deadlines_v1;
/* control_claim and bulk calls keep their existing signatures, but a host
 * advertising this suffix promises the same TOTAL timeout contract for them.
 * Do not advertise by wrapping legacy unbounded discovery/claim/release. */
#ifdef __cplusplus
}
#endif
