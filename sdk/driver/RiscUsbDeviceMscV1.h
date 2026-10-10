#pragma once
/* Owner-task USB device MSC control. The computer is the USB host. App callers
 * never supply callbacks/buffers to be retained. The provider owns its USB stack
 * and SD lease until checked cleanup. API registration/start does not export SD. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_USB_DEVICE_MSC_CAPABILITY "usb.device.msc"
#define RISC_USB_DEVICE_MSC_API_V1 1u
enum {
    RISC_USB_MSC_OK = 0,
    RISC_USB_MSC_REFUSED = -1,
    RISC_USB_MSC_RETAINED = -2,
    RISC_USB_MSC_IO_ERROR = -3
};
enum {
    RISC_USB_MSC_IDLE = 0,
    RISC_USB_MSC_WAITING = 1,
    RISC_USB_MSC_CONNECTED = 2,
    RISC_USB_MSC_SUSPENDED = 3,
    RISC_USB_MSC_EJECTED = 4,
    RISC_USB_MSC_DISCONNECTED = 5,
    RISC_USB_MSC_MEDIA_UNAVAILABLE = 6,
    RISC_USB_MSC_FAULT_RETAINED = 7,
    RISC_USB_MSC_PREPARING = 8
};
enum {
    /* Cancellation is accepted only before USB configuration; once a host has
     * mounted the volume, eject on the computer. This is not a force-unmount. */
    RISC_USB_MSC_END_CANCEL_WAITING = 0,
    /* Only after the user confirms the cable has physically been removed. The
     * X4 has no verified VBUS detector. Bus silence/suspend is not unplug proof. */
    RISC_USB_MSC_END_CABLE_REMOVED = 1
};
typedef struct {
    uint32_t struct_size, state;
    uint64_t blocks_read, blocks_written;
    uint8_t local_media_ready;
    uint8_t reserved[7];
} risc_usb_device_msc_status_v1;
typedef struct {
    uint32_t api_version, struct_size;
    void *context;
    /* A nonzero token on failure is retained custody. Keep this grant, app and
     * provider mapped. Normal refusal returns zero and permits ordinary UI. */
    int32_t (*begin)(void *context, uint64_t *token);
    /* Poll at least every few milliseconds while exported. Blocks are serviced
     * synchronously by the same owner task as the SD provider. Host eject ends
     * the session automatically, but the token remains valid for status/end. */
    int32_t (*poll)(void *context, uint64_t token, risc_usb_device_msc_status_v1 *out);
    /* OK consumes token only after transport, SD, and PHY cleanup all succeed.
     * REFUSED leaves session unchanged; RETAINED forbids unload/sleep/navigation. */
    int32_t (*end)(void *context, uint64_t token, uint32_t reason);
    bool (*last_error)(void *context, char *out, size_t capacity);
} risc_usb_device_msc_api_v1;
/* Explicit long preparation, invoked only from a settled preparation screen.
 * begin returns OK + session token in PREPARING. poll remains RAM-only in that
 * state. prepare_step completes at most one <=4095-byte checked SD append and
 * close under a 15-second transaction guard (one in-flight sector may finish
 * after the deadline), or final sync/unmount and controller handoff. It returns
 * OK while still PREPARING or once WAITING; a cleanly cancelled failure is
 * MEDIA_UNAVAILABLE. Inspect poll for the new state and acknowledge terminal
 * status with end. The SD trace cutoff is frozen before begin returns.
 * end(CANCEL_WAITING) safely cancels preparation and consumes the session only
 * after checked SD custody return. A failed cleanup retains the session.
 * No progress pointers are retained. blocks_read/written count host I/O only.
 */
#define RISC_USB_MSC_PREPARE_TAG UINT32_C(0x554d5031)
typedef struct {
    risc_usb_device_msc_api_v1 base;
    uint32_t prepare_tag, prepare_version;
    int32_t (*prepare_step)(void *context, uint64_t token);
} risc_usb_device_msc_api_v1_prepare;
static inline const risc_usb_device_msc_api_v1_prepare *risc_usb_device_msc_prepare(
    const risc_usb_device_msc_api_v1 *api) {
    if(!api || api->api_version!=1 || api->struct_size<sizeof(risc_usb_device_msc_api_v1_prepare))return NULL;
    const risc_usb_device_msc_api_v1_prepare *p=(const risc_usb_device_msc_api_v1_prepare *)api;
    return p->prepare_tag==RISC_USB_MSC_PREPARE_TAG && p->prepare_version==1 && p->prepare_step?p:NULL;
}
/* Owner-task RAM-only diagnostics; neither polls nor performs storage I/O.
 * Counts restart per session. Completed fields identify the latest acknowledged
 * CSW; count deltas expose commands coalesced between client snapshots. */
#define RISC_USB_MSC_DIAGNOSTICS_TAG UINT32_C(0x554d4431)
enum {
    RISC_USB_MSC_DIAG_CONFIGURED=1u, RISC_USB_MSC_DIAG_MEDIA_FAULT=2u,
    RISC_USB_MSC_DIAG_CONTROLLER_HEALTHY=4u, RISC_USB_MSC_DIAG_COMMAND_ACTIVE=8u
};
typedef struct {
    uint32_t struct_size, flags;
    uint64_t commands_started, commands_completed, blocks_read, blocks_written;
    uint64_t command_started_ms, last_command_elapsed_ms, last_io_elapsed_ms;
    uint64_t last_poll_gap_ms, max_poll_gap_ms;
    uint32_t current_opcode, current_tag, command_bytes, current_lba, current_block_count;
    uint32_t completed_opcode, completed_tag, last_csw_status;
    int32_t last_io_result;
    uint32_t last_io_lba, last_io_count, stalls, last_pump_passes;
} risc_usb_device_msc_diagnostics_v1;
/* Optional diagnostics record suffix. Set base.struct_size=sizeof(v2), then
 * inspect returned size. An old provider copies only v1. Actual BOT bytes are
 * distinct from successful SD blocks; WRITE data can be received before a media
 * failure. Timeout counters describe device/controller waits, not host timers.
 * This does not infer safe removal from suspend, deconfigure or bus reset. */
#define RISC_USB_MSC_DIAGNOSTICS_V2 2u
enum { RISC_USB_MSC_ABORT_BOT=1, RISC_USB_MSC_ABORT_STACK=2,
       RISC_USB_MSC_ABORT_BUS=3, RISC_USB_MSC_ABORT_UNPLUG=4 };
enum { RISC_USB_MSC_TIMEOUT_REGISTER=1, RISC_USB_MSC_TIMEOUT_IN=2 };
typedef struct {
    risc_usb_device_msc_diagnostics_v1 base;
    uint32_t transferred_bytes,residue,completed_lba,completed_blocks;
    uint32_t completed_requested,completed_transferred,completed_residue;
    uint32_t sense_key,sense_asc,sense_ascq,last_sense_key,last_sense_asc,last_sense_ascq;
    uint32_t sense_opcode,sense_lba,aborts,last_abort,timeouts,last_timeout;
    uint64_t timeout_ms,total_command_ms,max_command_ms,total_io_ms,max_io_ms;
    uint32_t resets,unconfigures,suspends,resumes,start_stop_flags,eject_requested,eject_complete;
    uint32_t data_opcode,data_lba,data_requested,data_transferred,data_residue;
    uint64_t data_elapsed_ms;
} risc_usb_device_msc_diagnostics_v2;
typedef struct {
    risc_usb_device_msc_api_v1_prepare base;
    uint32_t diagnostics_tag, diagnostics_version;
    int32_t (*diagnostics)(void *context, uint64_t token, risc_usb_device_msc_diagnostics_v1 *out);
} risc_usb_device_msc_api_v1_diagnostics;
static inline const risc_usb_device_msc_api_v1_diagnostics *risc_usb_device_msc_diagnostics(
    const risc_usb_device_msc_api_v1 *api) {
    if(!risc_usb_device_msc_prepare(api) || api->struct_size<sizeof(risc_usb_device_msc_api_v1_diagnostics))return NULL;
    const risc_usb_device_msc_api_v1_diagnostics *p=(const risc_usb_device_msc_api_v1_diagnostics *)api;
    return p->diagnostics_tag==RISC_USB_MSC_DIAGNOSTICS_TAG && p->diagnostics_version==1 && p->diagnostics?p:NULL;
}
#ifdef __cplusplus
}
#endif
