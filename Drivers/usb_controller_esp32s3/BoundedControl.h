#pragma once
#include "BoundedIdf.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Private single-owner EP0 operations on an already configured/referenced
 * device. Submission borrows only controller-owned DMA; completion returns
 * that exact URB once, after restoring the saved SDK pipe callback. */
esp_err_t risc_usb_control_submit(usb_host_client_handle_t, usb_transfer_t *, uint64_t request);
esp_err_t risc_usb_control_poll(uint64_t request);
esp_err_t risc_usb_control_cancel(uint64_t request);
void risc_usb_control_retain(uint64_t request);
bool risc_usb_control_busy(void);
bool risc_usb_control_faulted(void);
#ifdef __cplusplus
}
#endif
