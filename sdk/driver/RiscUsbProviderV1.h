#pragma once
/* Driver-only ABI. RiscRTE core MUST treat these capability names and API
 * pointers as opaque; the USB host implementation belongs in another ELF. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define RISC_PROVIDER_DRIVER_ABI_V2 2u
#define RISC_USB_HOST_API_V1 1u
#define RISC_USB_CDC_API_V1 1u
#define RISC_USB_CONFIG_LIMIT 4096u
#define RISC_USB_CDC_MAX_SESSIONS 4u

/* Device IDs and claims are host-ELF-owned generation-safe tokens, never IDF
 * pointers. Negative transfer results mean failure; zero means no data. */
typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    void *context;
    bool (*configuration)(void *context, uint64_t device, uint8_t *bytes,
                          size_t *inout_length, uint16_t *vid, uint16_t *pid);
    bool (*claim)(void *context, uint64_t device, uint8_t interface_number,
                  uint8_t alternate, uint64_t *claim);
    void (*release)(void *context, uint64_t claim);
    int32_t (*control)(void *context, uint64_t device, uint8_t request_type,
                       uint8_t request, uint16_t value, uint16_t index,
                       uint8_t *payload, uint16_t length, uint32_t timeout_ms);
    int32_t (*bulk_read)(void *context, uint64_t claim, uint8_t endpoint,
                         uint8_t *dst, size_t capacity, uint32_t timeout_ms);
    int32_t (*bulk_write)(void *context, uint64_t claim, uint8_t endpoint,
                          const uint8_t *src, size_t length, uint32_t timeout_ms);
} risc_usb_host_api_v1;

/* A driver owns hardware sessions; the runtime owns only generic grants and
 * dispatch lifetime. A zero session is invalid. All calls are serialized by
 * the provider executor until a future ABI explicitly supports concurrency. */
typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    uint64_t (*open)(uint64_t device);
    bool (*configure)(uint64_t session, uint32_t baud, uint8_t data_bits,
                      uint8_t parity, uint8_t stop_bits);
    bool (*control_lines)(uint64_t session, bool dtr, bool rts);
    int32_t (*read)(uint64_t session, uint8_t *dst, size_t capacity,
                    uint32_t timeout_ms);
    int32_t (*write)(uint64_t session, const uint8_t *src, size_t length,
                     uint32_t timeout_ms);
    bool (*close)(uint64_t session);
} risc_usb_cdc_api_v1;

/* Generic provider dependencies are resolved by ID/version, not by a built-in
 * USB branch. The containing loader must pin both ELFs until all sessions
 * close, revoke dependent handles on loss, and call stop before unmapping. */
typedef struct {
    const char *capability_id;
    uint32_t api_version;
    const void *api;
} risc_provider_dependency_v1;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    const char *driver_id;
    const char *capability_id;
    uint32_t capability_api;
    const void *capability;
    bool (*start)(const risc_provider_dependency_v1 *dependencies, size_t count);
    void (*stop)(void);
} risc_driver_v2;

typedef const risc_driver_v2 *(*risc_driver_get_v2_fn)(uint32_t abi);
/* ABI v2 is intentionally rejected by the current ABI-v1 USB loader. It may
 * only be activated when the generic dependency-aware provider loader exists. */
const risc_driver_v2 *t5_driver_get(uint32_t abi);
#ifdef __cplusplus
}
#endif
