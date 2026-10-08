#pragma once
/* Private ESP-IDF v4.4.7 adaptation, NOT a published controller capability.
 * All calls are try/defer, with controller-owned callbacks and serialized
 * ownership. No function installs, claims, closes, or processes HUB/USBH work.
 * ESP_ERR_NOT_FINISHED means no proof of completion, never an empty bus. */
#include <usb/usb_host.h>
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t risc_usb_host_library_idle(usb_host_client_handle_t);
/* One owned bulk retirement. Other endpoints/control/client messages defer.
 * progressed=false/OK proves only the client's queues empty at that instant. */
esp_err_t risc_usb_host_client_step(usb_host_client_handle_t, void *owned_endpoint,
                                  bool *progressed);
esp_err_t risc_usb_host_bulk_resolve(usb_host_client_handle_t, usb_device_handle_t,
                                   uint8_t, uint8_t interface_number, uint8_t alternate,
                                   void **endpoint, uint16_t *packet);
esp_err_t risc_usb_host_bulk_submit(void *endpoint, usb_transfer_t *);
esp_err_t risc_usb_host_bulk_cancel_step(void *endpoint);
esp_err_t risc_usb_host_bulk_clear(void *endpoint);
#ifdef __cplusplus
}
#endif
