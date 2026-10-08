/* SPDX-FileCopyrightText: 2015-2022 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 * Private bounded steps beside (not replacements for) the v4.4.7 API.
 * The controller is the sole client/owner; callbacks may only copy bounded
 * records or update its owned DMA state. No callback may call a legacy SDK
 * operation. The host lock is held before HCD access, including pipe callbacks.
 */
extern esp_err_t risc_hcd_bulk_enqueue(hcd_pipe_handle_t, urb_t *);
extern esp_err_t risc_hcd_dequeue_step(hcd_pipe_handle_t, bool, urb_t **, bool *);
extern esp_err_t risc_hcd_bulk_cancel_step(hcd_pipe_handle_t);
extern esp_err_t risc_hcd_bulk_clear(hcd_pipe_handle_t);

esp_err_t risc_usb_host_library_idle(usb_host_client_handle_t handle)
{
    if (!xPortEnterCriticalTimeout(&host_lock, 0)) return ESP_ERR_NOT_FINISHED;
    esp_err_t ret = ESP_ERR_INVALID_STATE;
    if (p_host_lib_obj && handle) {
        client_t *client = (client_t *)handle;
        ret = (client->dynamic.bounded_num_messages ||
               client->dynamic.num_done_ctrl_xfer ||
               !TAILQ_EMPTY(&client->dynamic.pending_ep_tailq) ||
               client->dynamic.flags.handling_events ||
               client->dynamic.flags.blocked ||
               client->dynamic.flags.taking_mux ||p_host_lib_obj->dynamic.process_pending_flags ||
               p_host_lib_obj->dynamic.lib_event_flags ||
               p_host_lib_obj->dynamic.flags.handling_events ||
               p_host_lib_obj->dynamic.flags.process_pending) ?
                ESP_ERR_NOT_FINISHED : ESP_OK;
    }
    HOST_EXIT_CRITICAL();
    /* Preserve every flag. HUB/root-port and USBH lifecycle are not bounded
     * in this slice. Incomplete work cannot be consumed as an empty bus. */
    return ret;
}

esp_err_t risc_usb_host_client_step(usb_host_client_handle_t handle, void *owned_endpoint, bool *progressed)
{
    if (!handle || !owned_endpoint || !progressed) return ESP_ERR_INVALID_ARG;
    *progressed = false;
    if (!xPortEnterCriticalTimeout(&host_lock, 0)) return ESP_ERR_NOT_FINISHED;
    client_t *client = (client_t *)handle;
    if (client->dynamic.flags.handling_events || client->dynamic.flags.blocked ||
        client->dynamic.flags.taking_mux) {
        HOST_EXIT_CRITICAL();
        return ESP_ERR_NOT_FINISHED;
    }
    endpoint_t *ep = (endpoint_t *)owned_endpoint;
    if (ep->constant.intf_obj->constant.client_obj != client) {
        HOST_EXIT_CRITICAL();
        return ESP_ERR_INVALID_ARG;
    }
    urb_t *urb = NULL;
    if (ep->dynamic.flags.pending && ep->dynamic.flags.bounded_owned) {
        hcd_pipe_event_t event = ep->dynamic.last_event;
        bool flush = event == HCD_PIPE_EVENT_ERROR_XFER ||
                     event == HCD_PIPE_EVENT_ERROR_URB_NOT_AVAIL ||
                     event == HCD_PIPE_EVENT_ERROR_OVERFLOW ||
                     event == HCD_PIPE_EVENT_ERROR_STALL;
        if (!flush && event != HCD_PIPE_EVENT_URB_DONE) {
            HOST_EXIT_CRITICAL();
            return ESP_ERR_INVALID_STATE;
        }
        bool empty = false;
        esp_err_t ret = risc_hcd_dequeue_step(ep->constant.pipe_hdl, flush, &urb, &empty);
        if (ret != ESP_OK) { HOST_EXIT_CRITICAL(); return ret; }
        if (urb) {
            assert(ep->dynamic.num_urb_inflight > 0);
            ep->dynamic.num_urb_inflight--;
        }
        if (empty) {
            TAILQ_REMOVE(&client->dynamic.pending_ep_tailq, ep, dynamic.tailq_entry);
            TAILQ_INSERT_TAIL(&client->dynamic.idle_ep_tailq, ep, dynamic.tailq_entry);
            ep->dynamic.flags.pending = 0;
        }
        /* Retiring one empty endpoint is also a work item. HCD emptiness is
         * observed under its lock; later IRQs requeue after host_lock exits. */
        *progressed = true;
    }
    const bool deferred = client->dynamic.bounded_num_messages ||
        client->dynamic.num_done_ctrl_xfer || !TAILQ_EMPTY(&client->dynamic.pending_ep_tailq);
    if (!deferred) client->dynamic.flags.events_pending = 0;
    if (urb) urb->usb_host_inflight = false;
    HOST_EXIT_CRITICAL();
    if (urb) urb->transfer.callback(&urb->transfer);
    return deferred ? ESP_ERR_NOT_FINISHED : ESP_OK;
}

esp_err_t risc_usb_host_bulk_resolve(usb_host_client_handle_t client,
                                    usb_device_handle_t dev, uint8_t address,
                                    uint8_t interface_number, uint8_t alternate,
                                    void **out, uint16_t *packet)
{
    if (!client || !dev || !out || !packet) return ESP_ERR_INVALID_ARG;
    if (!xPortEnterCriticalTimeout(&host_lock, 0)) return ESP_ERR_NOT_FINISHED;
    /* The controller is this private SDK instance's sole client and task
     * owner. Interface lists are mutated only by that same serialized owner;
     * ISR code touches only the separately guarded dynamic endpoint state.
     * No claim/release/device close can run while the request owns storage.
     * Do not take a FreeRTOS mutex: even its zero-wait take reaches scheduler
     * spinlocks through priority inheritance. Bound scans before each access. */
    client_t *owner = (client_t *)client;
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    if (!p_host_lib_obj || p_host_lib_obj->dynamic.flags.num_clients != 1 ||
        owner->dynamic.flags.taking_mux || owner->dynamic.flags.handling_events ||
        owner->dynamic.flags.blocked) {
        HOST_EXIT_CRITICAL();
        return ESP_ERR_NOT_FINISHED;
    }
    interface_t *intf = TAILQ_FIRST(&owner->mux_protected.interface_tailq);
    for (unsigned n = 0; intf && n < 16; ++n, intf = TAILQ_NEXT(intf, mux_protected.tailq_entry)) {
        if (intf->constant.client_obj != owner || intf->constant.dev_hdl != dev ||
            intf->constant.intf_desc->bInterfaceNumber != interface_number ||
            intf->constant.intf_desc->bAlternateSetting != alternate) continue;
        unsigned count = intf->constant.intf_desc->bNumEndpoints;
        if (count > 30) { ret = ESP_ERR_INVALID_STATE; break; }
        for (unsigned i = 0; i < count; ++i) {
            endpoint_t *ep = intf->constant.endpoints[i];
            if (!ep || ep->constant.ep_desc->bEndpointAddress != address) continue;
            if (ep->dynamic.num_urb_inflight || ep->dynamic.flags.pending ||
                USB_EP_DESC_GET_XFERTYPE(ep->constant.ep_desc) != USB_TRANSFER_TYPE_BULK)
                ret = ESP_ERR_INVALID_STATE;
            else {
                *packet = USB_EP_DESC_GET_MPS(ep->constant.ep_desc);
                *out = ep; ret = ESP_OK;
            }
            break;
        }
        break;
    }
    HOST_EXIT_CRITICAL();
    return ret;
}

esp_err_t risc_usb_host_bulk_submit(void *endpoint, usb_transfer_t *transfer)
{
    if (!endpoint || !transfer || !transfer->callback || !transfer->device_handle ||
        transfer->num_bytes <= 0 || (size_t)transfer->num_bytes > transfer->data_buffer_size ||
        transfer->num_isoc_packets != 0) return ESP_ERR_INVALID_ARG;
    if (!xPortEnterCriticalTimeout(&host_lock, 0)) return ESP_ERR_NOT_FINISHED;
    endpoint_t *ep = (endpoint_t *)endpoint;
    urb_t *urb = __containerof(transfer, urb_t, transfer);
    esp_err_t ret = ESP_ERR_INVALID_STATE;
    const uint16_t packet = USB_EP_DESC_GET_MPS(ep->constant.ep_desc);
    if (!urb->usb_host_inflight && !ep->dynamic.num_urb_inflight &&
        transfer->device_handle == ep->constant.intf_obj->constant.dev_hdl &&
        transfer->bEndpointAddress == ep->constant.ep_desc->bEndpointAddress &&
        USB_EP_DESC_GET_XFERTYPE(ep->constant.ep_desc) == USB_TRANSFER_TYPE_BULK &&
        packet && packet <= 512 &&
        (!(transfer->bEndpointAddress & 0x80u) || transfer->num_bytes % packet == 0)) {
        ep->dynamic.flags.bounded_owned = 1;
        urb->usb_host_inflight = true;
        ep->dynamic.num_urb_inflight++;
        ret = risc_hcd_bulk_enqueue(ep->constant.pipe_hdl, urb);
        if (ret != ESP_OK) {
            ep->dynamic.flags.bounded_owned = 0;
            urb->usb_host_inflight = false;
            ep->dynamic.num_urb_inflight--;
        }
    }
    HOST_EXIT_CRITICAL();
    return ret;
}

esp_err_t risc_usb_host_bulk_cancel_step(void *endpoint)
{
    if (!endpoint) return ESP_ERR_INVALID_ARG;
    if (!xPortEnterCriticalTimeout(&host_lock, 0)) return ESP_ERR_NOT_FINISHED;
    esp_err_t ret = risc_hcd_bulk_cancel_step(((endpoint_t *)endpoint)->constant.pipe_hdl);
    HOST_EXIT_CRITICAL();
    return ret;
}

esp_err_t risc_usb_host_bulk_clear(void *endpoint)
{
    if (!endpoint) return ESP_ERR_INVALID_ARG;
    if (!xPortEnterCriticalTimeout(&host_lock, 0)) return ESP_ERR_NOT_FINISHED;
    esp_err_t ret = risc_hcd_bulk_clear(((endpoint_t *)endpoint)->constant.pipe_hdl);
    HOST_EXIT_CRITICAL();
    return ret;
}
