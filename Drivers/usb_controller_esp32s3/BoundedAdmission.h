#pragma once
#include "BoundedIdf.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Private, explicit prerequisite. Preparation and disposal may allocate/free
 * internal/DMA memory and are NOT deadline-qualified operations. No bounded
 * operation calls either implicitly. The public suffix remains absent. */
esp_err_t risc_usb_admission_prepare(void);
esp_err_t risc_usb_admission_dispose(void);
size_t risc_usb_admission_internal_bytes(void);
size_t risc_usb_admission_dma_bytes(void);
bool risc_usb_admission_owns_resources(void);
bool risc_usb_admission_faulted(void);
esp_err_t risc_usb_configuration_copy(usb_host_client_handle_t, usb_device_handle_t,
    uint8_t *, size_t, size_t *, uint16_t *, uint16_t *);
esp_err_t risc_usb_claim_begin(usb_host_client_handle_t, usb_device_handle_t,
    uint8_t interface_number, uint8_t alternate, uint64_t token);
/* One endpoint acquisition/retirement or one final metadata commit per step.
 * NOT_FINISHED preserves exact custody. Failure after acquisition is retained
 * until explicit bounded rollback/release succeeds within its original budget. */
esp_err_t risc_usb_claim_step(uint64_t token);
esp_err_t risc_usb_claim_release_step(uint64_t token);
void risc_usb_claim_retain(uint64_t token);
bool risc_usb_claim_abandon_unacquired(uint64_t token);
typedef struct {
    uint64_t token;
    uint32_t endpoints, acquired;
    bool published, retained;
} risc_usb_claim_state;
bool risc_usb_claim_state_copy(uint64_t token, risc_usb_claim_state *);
/* Logical reference close only. deferred=true means SDK free/recover/disable
 * remains pending. The controller must retain that separate custody; success
 * does not qualify physical device destruction or host/provider shutdown. */
esp_err_t risc_usb_device_close_try(usb_host_client_handle_t, usb_device_handle_t,
                                   bool *reference_closed, bool *deferred);
#ifdef __cplusplus
}
#endif
