/* SPDX-FileCopyrightText: 2015-2022 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 * Called only by the private single-owner host adaptation with host_lock held.
 * No FreeRTOS mutex, logger, allocator or notification is reached here. */
extern esp_err_t risc_hcd_pool_allocate(hcd_port_handle_t, const hcd_pipe_config_t *, hcd_pipe_handle_t *);
extern esp_err_t risc_hcd_pool_release(hcd_pipe_handle_t);

esp_err_t risc_usbh_device_address_try(usb_device_handle_t handle, uint8_t *address)
{
    if (!handle || !address) return ESP_ERR_INVALID_ARG;
    if (!xPortEnterCriticalTimeout(&usbh_lock, 0)) return ESP_ERR_NOT_FINISHED;
    device_t *dev = (device_t *)handle;
    esp_err_t ret = ESP_ERR_INVALID_STATE;
    if (dev->dynamic.ref_count && dev->constant.address) { *address = dev->constant.address; ret = ESP_OK; }
    USBH_EXIT_CRITICAL(); return ret;
}

esp_err_t risc_usbh_configuration_copy(usb_device_handle_t handle, uint8_t *out,
                                      size_t capacity, size_t *length,
                                      uint16_t *vid, uint16_t *pid, uint8_t *address)
{
    if (!handle || !out || !length || !vid || !pid || !address) return ESP_ERR_INVALID_ARG;
    if (!xPortEnterCriticalTimeout(&usbh_lock, 0)) return ESP_ERR_NOT_FINISHED;
    device_t *dev = (device_t *)handle;
    esp_err_t ret = ESP_ERR_INVALID_STATE;
    if (dev->dynamic.state != USB_DEVICE_STATE_CONFIGURED || dev->dynamic.flags.is_gone ||
        !dev->dynamic.ref_count || !dev->constant.desc || !dev->constant.config_desc) goto exit;
    size_t size = dev->constant.config_desc->wTotalLength;
    if (size < 9 || size > 4096 || capacity < size) { ret = ESP_ERR_INVALID_SIZE; goto exit; }
    memcpy(out, dev->constant.config_desc, size);
    *length = size; *vid = dev->constant.desc->idVendor; *pid = dev->constant.desc->idProduct;
    *address = dev->constant.address; ret = ESP_OK;
exit:
    USBH_EXIT_CRITICAL(); return ret;
}

esp_err_t risc_usbh_pipe_allocate(usb_device_handle_t handle, const usbh_ep_config_t *config,
                                 hcd_pipe_handle_t *out)
{
    if (!handle || !config || !config->ep_desc || !out) return ESP_ERR_INVALID_ARG;
    uint8_t address = config->ep_desc->bEndpointAddress & 0x0fu;
    if (!address || (config->ep_desc->bEndpointAddress & 0x70u)) return ESP_ERR_INVALID_ARG;
    if (!xPortEnterCriticalTimeout(&usbh_lock, 0)) return ESP_ERR_NOT_FINISHED;
    device_t *dev = (device_t *)handle;
    hcd_pipe_handle_t *target = config->ep_desc->bEndpointAddress & 0x80u ?
        &dev->mux_protected.ep_in[address - 1] : &dev->mux_protected.ep_out[address - 1];
    esp_err_t ret = ESP_ERR_INVALID_STATE;
    if (*target || dev->dynamic.state != USB_DEVICE_STATE_CONFIGURED ||
        dev->dynamic.flags.is_gone || !dev->dynamic.ref_count) goto exit;
    hcd_pipe_config_t pipe = {
        .callback = config->pipe_cb, .callback_arg = config->pipe_cb_arg,
        .context = config->context, .ep_desc = config->ep_desc,
        .dev_speed = dev->constant.speed, .dev_addr = dev->constant.address,
    };
    hcd_pipe_handle_t acquired = NULL;
    ret = risc_hcd_pool_allocate(dev->constant.port_hdl, &pipe, &acquired);
    if (ret == ESP_OK) { *target = acquired; *out = acquired; }
exit:
    USBH_EXIT_CRITICAL(); return ret;
}

esp_err_t risc_usbh_pipe_release(usb_device_handle_t handle, uint8_t endpoint,
                                hcd_pipe_handle_t expected)
{
    uint8_t address = endpoint & 0x0fu;
    if (!handle || !address || (endpoint & 0x70u) || !expected) return ESP_ERR_INVALID_ARG;
    if (!xPortEnterCriticalTimeout(&usbh_lock, 0)) return ESP_ERR_NOT_FINISHED;
    device_t *dev = (device_t *)handle;
    hcd_pipe_handle_t *target = endpoint & 0x80u ?
        &dev->mux_protected.ep_in[address - 1] : &dev->mux_protected.ep_out[address - 1];
    esp_err_t ret = ESP_ERR_INVALID_STATE;
    if (*target == expected) {
        ret = risc_hcd_pool_release(expected);
        if (ret == ESP_OK) *target = NULL;
    }
    USBH_EXIT_CRITICAL(); return ret;
}

esp_err_t risc_usbh_device_close_try(usb_device_handle_t handle, uint8_t *address,
                                    bool *deferred)
{
    if (!handle || !address || !deferred) return ESP_ERR_INVALID_ARG;
    if (!xPortEnterCriticalTimeout(&usbh_lock, 0)) return ESP_ERR_NOT_FINISHED;
    device_t *dev = (device_t *)handle;
    esp_err_t ret = ESP_ERR_INVALID_STATE;
    if (!dev->dynamic.ref_count || dev->dynamic.num_ctrl_xfers_inflight ||
        dev->dynamic.flags.waiting_free || !dev->constant.address) goto exit;
    /* Do not decrement a reference while earlier USBH work is still pending:
     * the stock _dev_set_actions helper does not append flags to such a node. */
    if (dev->dynamic.flags.in_pending_list || dev->dynamic.action_flags) {
        ret = ESP_ERR_NOT_FINISHED; goto exit;
    }
    --dev->dynamic.ref_count;
    *deferred = false; *address = dev->constant.address;
    if (!dev->dynamic.ref_count) {
        if (dev->dynamic.flags.is_gone) {
            dev->dynamic.flags.waiting_free = 1;
            (void)_dev_set_actions(dev, DEV_FLAG_ACTION_FREE_AND_RECOVER);
            *deferred = true;
        } else if (dev->dynamic.flags.waiting_close) {
            dev->dynamic.flags.waiting_port_disable = 1;
            (void)_dev_set_actions(dev, DEV_FLAG_ACTION_PORT_DISABLE);
            *deferred = true;
        }
    }
    /* The caller marks the host's pending-USBH flags under its held lock.
     * No _unblock_lib notification or semaphore is required by this owner. */
    ret = ESP_OK;
exit:
    USBH_EXIT_CRITICAL(); return ret;
}
