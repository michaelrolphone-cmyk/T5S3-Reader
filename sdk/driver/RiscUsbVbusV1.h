#pragma once
/* USB/board-specific provider SDK, never part of the generic RiscRTE core.
 * The board provider arbitrates OTG role, charger conflicts and source current.
 * A successful release must mean VBUS is electrically safe before ELF unload. */
#include "RiscProviderV2.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_USB_VBUS_API_V1 1u
typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    void *context;
    /* Refuse a lease when the board is connected to external USB power, the
     * charger is unsafe, the port is in device mode or another owner exists. */
    bool (*acquire_host)(void *context, uint32_t max_milliamps, uint64_t *lease);
    /* A failed release must retain ownership and prevent controller unmap. */
    bool (*release_host)(void *context, uint64_t lease);
    bool (*quiesce)(void *context);
} risc_usb_vbus_api_v1;
#ifdef __cplusplus
}
#endif
