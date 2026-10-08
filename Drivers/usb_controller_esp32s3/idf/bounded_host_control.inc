/* SPDX-FileCopyrightText: 2015-2022 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#include "BoundedControl.h"
extern esp_err_t risc_usbh_control_submit(usb_device_handle_t, urb_t *, uint64_t);
extern esp_err_t risc_usbh_control_cancel(uint64_t);
extern esp_err_t risc_usbh_control_poll(uint64_t, urb_t **);

esp_err_t risc_usb_control_submit(usb_host_client_handle_t handle, usb_transfer_t *transfer,
                                 uint64_t request)
{
    if (!handle || !transfer || !request || !transfer->callback || !transfer->data_buffer ||
        !transfer->device_handle || transfer->bEndpointAddress || transfer->num_isoc_packets ||
        transfer->num_bytes < 8 || transfer->num_bytes > 4104 ||
        (size_t)transfer->num_bytes > transfer->data_buffer_size) return ESP_ERR_INVALID_ARG;
    const uint8_t *setup = transfer->data_buffer;
    const unsigned length = setup[6] | ((unsigned)setup[7] << 8);
    if (length > 4096 || (unsigned)transfer->num_bytes != length + 8) return ESP_ERR_INVALID_ARG;
    esp_err_t ready = risc_usb_host_library_idle(handle);
    if (ready != ESP_OK) return ready;
    if (!xPortEnterCriticalTimeout(&host_lock, 0)) return ESP_ERR_NOT_FINISHED;
    client_t *client = (client_t *)handle;
    urb_t *urb = __containerof(transfer, urb_t, transfer);
    esp_err_t ret = ESP_ERR_INVALID_STATE;
    uint8_t address = 0;
    if (!p_host_lib_obj || p_host_lib_obj->dynamic.flags.num_clients != 1 ||
        risc_native_active || risc_native_callback_fault || urb->usb_host_inflight) goto exit;
    ret = risc_usbh_device_address_try(transfer->device_handle, &address);
    if (ret != ESP_OK) goto exit;
    if (!address || address > 32 || !_check_client_opened_device(client, address)) {
        ret = ESP_ERR_INVALID_STATE; goto exit;
    }
    urb->usb_host_inflight = true; urb->usb_host_client = handle;
    ret = risc_usbh_control_submit(transfer->device_handle, urb, request);
    if (ret != ESP_OK) { urb->usb_host_inflight = false; urb->usb_host_client = NULL; }
exit:
    HOST_EXIT_CRITICAL(); return ret;
}

esp_err_t risc_usb_control_poll(uint64_t request)
{
    if (!xPortEnterCriticalTimeout(&host_lock, 0)) return ESP_ERR_NOT_FINISHED;
    urb_t *urb = NULL;
    esp_err_t ret = risc_usbh_control_poll(request, &urb);
    if (urb) urb->usb_host_client = NULL;
    HOST_EXIT_CRITICAL();
    if (urb) urb->transfer.callback(&urb->transfer);
    return ret;
}

esp_err_t risc_usb_control_cancel(uint64_t request)
{
    if (!xPortEnterCriticalTimeout(&host_lock, 0)) return ESP_ERR_NOT_FINISHED;
    esp_err_t ret = risc_usbh_control_cancel(request);
    HOST_EXIT_CRITICAL(); return ret;
}
