/* SPDX-FileCopyrightText: 2015-2022 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
extern esp_err_t risc_hcd_control_start(hcd_pipe_handle_t, urb_t *, hcd_pipe_callback_t,
    void *, hcd_pipe_callback_t, void *, hcd_pipe_callback_t *, void **);
extern esp_err_t risc_hcd_control_cancel_step(hcd_pipe_handle_t);
extern esp_err_t risc_hcd_control_retire(hcd_pipe_handle_t, urb_t *);
extern esp_err_t risc_hcd_control_restore(hcd_pipe_handle_t, hcd_pipe_callback_t, void *,
                                        hcd_pipe_callback_t, void *);
typedef struct {
    uint64_t request;
    device_t *device;
    urb_t *urb;
    hcd_pipe_handle_t pipe;
    hcd_pipe_callback_t previous;
    void *previous_arg;
    uint32_t cookie;
    bool pending, retired, cancelling, cancel_acknowledged;
    volatile bool retained;
} risc_control_state;
static risc_control_state risc_control;
static uint32_t risc_control_serial; /* Never reset/reused in one loaded image. */
static volatile bool risc_control_fault;
__attribute__((used)) static const size_t risc_control_state_bytes = sizeof(risc_control_state);

static bool risc_usbh_control_owns(usb_device_handle_t handle)
{
    return risc_control.request && (usb_device_handle_t)risc_control.device == handle;
}
bool risc_usb_control_busy(void) { return risc_control.request || risc_control_fault; }
bool risc_usb_control_faulted(void) { return risc_control_fault || risc_control.retained; }

static bool risc_control_callback(hcd_pipe_handle_t pipe, hcd_pipe_event_t event,
                                  void *argument, bool isr)
{
    (void)isr;
    USBH_ENTER_CRITICAL_SAFE();
    const uint32_t cookie = (uint32_t)(uintptr_t)argument;
    if (!risc_control.request || !cookie || risc_control.cookie != cookie ||
        risc_control.pipe != pipe || risc_control.pending || risc_control.retired ||
        (event != HCD_PIPE_EVENT_URB_DONE && event != HCD_PIPE_EVENT_ERROR_XFER &&
         event != HCD_PIPE_EVENT_ERROR_URB_NOT_AVAIL && event != HCD_PIPE_EVENT_ERROR_OVERFLOW &&
         event != HCD_PIPE_EVENT_ERROR_STALL)) {
        risc_control_fault = true; risc_control.retained = true;
    } else {
        risc_control.pending = true;
    }
    USBH_EXIT_CRITICAL_SAFE();
    return false; /* No logger, USBH action, client queue or OS notification. */
}

esp_err_t risc_usbh_control_submit(usb_device_handle_t handle, urb_t *urb, uint64_t request)
{
    if (!handle || !urb || !request) return ESP_ERR_INVALID_ARG;
    if (!xPortEnterCriticalTimeout(&usbh_lock, 0)) return ESP_ERR_NOT_FINISHED;
    device_t *dev = (device_t *)handle;
    esp_err_t ret = ESP_ERR_INVALID_STATE;
    if (risc_control.request || risc_control_fault || risc_control_serial == UINT32_MAX ||
        dev->dynamic.state != USB_DEVICE_STATE_CONFIGURED || dev->dynamic.flags.is_gone ||
        !dev->dynamic.ref_count || dev->dynamic.num_ctrl_xfers_inflight ||
        dev->dynamic.flags.in_pending_list || dev->dynamic.action_flags || !dev->constant.default_pipe)
        goto exit;
    memset(&risc_control, 0, sizeof(risc_control));
    risc_control.request = request; risc_control.device = dev; risc_control.urb = urb;
    risc_control.pipe = dev->constant.default_pipe; risc_control.cookie = ++risc_control_serial;
    dev->dynamic.num_ctrl_xfers_inflight++;
    ret = risc_hcd_control_start(risc_control.pipe, urb, risc_control_callback,
        (void *)(uintptr_t)risc_control.cookie, default_pipe_callback, dev,
        &risc_control.previous, &risc_control.previous_arg);
    if (ret != ESP_OK) {
        dev->dynamic.num_ctrl_xfers_inflight--;
        memset(&risc_control, 0, sizeof(risc_control));
    }
exit:
    USBH_EXIT_CRITICAL(); return ret;
}

esp_err_t risc_usbh_control_cancel(uint64_t request)
{
    if (!xPortEnterCriticalTimeout(&usbh_lock, 0)) return ESP_ERR_NOT_FINISHED;
    esp_err_t ret = ESP_ERR_INVALID_STATE;
    if (!request || request != risc_control.request || risc_control.retained || risc_control_fault) goto exit;
    risc_control.cancelling = true;
    if (risc_control.retired || risc_control.cancel_acknowledged) { ret = ESP_OK; goto exit; }
    ret = risc_hcd_control_cancel_step(risc_control.pipe);
    if (ret == ESP_OK) risc_control.cancel_acknowledged = true;
    else if (ret != ESP_ERR_NOT_FINISHED) risc_control.retained = true;
exit:
    USBH_EXIT_CRITICAL(); return ret;
}

esp_err_t risc_usbh_control_poll(uint64_t request, urb_t **completed)
{
    *completed = NULL;
    if (!xPortEnterCriticalTimeout(&usbh_lock, 0)) return ESP_ERR_NOT_FINISHED;
    esp_err_t ret = ESP_ERR_INVALID_STATE;
    if (!request || request != risc_control.request || risc_control.retained || risc_control_fault) goto exit;
    if (risc_control.cancelling && !risc_control.cancel_acknowledged && !risc_control.retired) {
        ret = ESP_ERR_NOT_FINISHED; goto exit;
    }
    if (!risc_control.retired) {
        /* Wait for delivery of the actual pipe callback, not merely a done
         * queue observed before its ISR callback has reached this owner. */
        if (!risc_control.pending) { ret = ESP_ERR_NOT_FINISHED; goto exit; }
        ret = risc_hcd_control_retire(risc_control.pipe, risc_control.urb);
        if (ret == ESP_OK) {
            risc_control.retired = true; risc_control.pending = false;
            ret = ESP_ERR_NOT_FINISHED;
        } else if (ret != ESP_ERR_NOT_FINISHED) risc_control.retained = true;
        goto exit;
    }
    if (risc_control.device->dynamic.num_ctrl_xfers_inflight != 1 ||
        !risc_control.urb->usb_host_inflight) {
        risc_control.retained = true; ret = ESP_ERR_INVALID_STATE; goto exit;
    }
    ret = risc_hcd_control_restore(risc_control.pipe, risc_control_callback,
        (void *)(uintptr_t)risc_control.cookie, risc_control.previous, risc_control.previous_arg);
    if (ret == ESP_OK) {
        risc_control.device->dynamic.num_ctrl_xfers_inflight--;
        risc_control.urb->usb_host_inflight = false;
        *completed = risc_control.urb;
        memset(&risc_control, 0, sizeof(risc_control));
    } else if (ret != ESP_ERR_NOT_FINISHED) risc_control.retained = true;
exit:
    USBH_EXIT_CRITICAL(); return ret;
}

void risc_usb_control_retain(uint64_t request)
{
    /* Serialized owner writes a terminal bit; IRQ callbacks only OR retention
     * or publish pending. No lock/notification/cleanup is needed to fence it. */
    if (request && risc_control.request == request) risc_control.retained = true;
}
