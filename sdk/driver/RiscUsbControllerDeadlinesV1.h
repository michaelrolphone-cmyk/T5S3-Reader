#pragma once
/* Optional physical-controller contract. A size increase alone cannot grant
 * it. Preserve the COMPLETE deployed interrupt/diagnostic prefix. */
#include "RiscUsbDiscoveryDiagnosticsV1.h"
#include "RiscStreamResultV1.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_USB_CONTROLLER_DEADLINES_TAG_V1 UINT32_C(0x55434431)
#define RISC_USB_CONTROLLER_DEADLINES_API_V1 1u
#define RISC_USB_DEADLINE_MAX_MS 1000u

typedef struct {
    uint32_t api_version, struct_size;
    uint64_t (*now_ms)(void *context);
    /* TOTAL caller budget, including lock admission, SDK event processing,
     * enumeration, role/power changes and cleanup. 1 copies one event; 0 proves
     * the current event inventory complete; negative is a stream result.
     * Internal event work must also have an explicit item bound. */
    int32_t (*next_event)(void *, risc_usb_controller_event_v1 *, uint32_t);
    int32_t (*configuration)(void *, uint64_t, uint8_t *, size_t *,
                             uint16_t *, uint16_t *, uint32_t);
    /* OK: fresh nonzero claim; clean failure: zero; RETAINED: exact remaining
     * claim if known, otherwise zero with controller dependency retained. */
    int32_t (*claim)(void *, uint64_t, uint8_t, uint8_t, uint64_t *, uint32_t);
    /* Only OK proves no interface, DMA or callback custody remains. */
    int32_t (*release)(void *, uint64_t, uint32_t);
    /* Return completed copied bytes or a negative stream result. No caller
     * pointer may survive return. Cancellation/timeout that cannot establish
     * physical completion reports RETAINED and pins the controller and DMA. */
    int32_t (*control)(void *, uint64_t, uint8_t, uint8_t, uint16_t, uint16_t,
                       uint8_t *, uint16_t, uint32_t);
    int32_t (*bulk_read)(void *, uint64_t, uint8_t, uint8_t *, size_t, uint32_t);
    int32_t (*bulk_write)(void *, uint64_t, uint8_t, const uint8_t *, size_t, uint32_t);
} risc_usb_controller_deadline_api_v1;
typedef struct {
    risc_usb_controller_diagnostics_v1 diagnostics;
    uint32_t extension_tag, extension_version;
    const risc_usb_controller_deadline_api_v1 *deadlines;
} risc_usb_controller_deadlines_v1;
/* now_ms is nonblocking, monotonic and cannot reset while started. Every
 * positive budget is 1..1000 ms and includes all work. No callback may retain
 * any caller pointer after return, including event/claim/result buffers.
 * Never advertise this
 * suffix by timing a legacy SDK call afterward, or while any called SDK path
 * has a fixed wait, unbounded wait/work, or uncertain unreported DMA custody. */
#ifdef __cplusplus
}
#endif
