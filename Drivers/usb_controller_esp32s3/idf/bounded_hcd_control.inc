/* SPDX-FileCopyrightText: 2015-2022 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 * Caller holds host + USBH locks. Private control callbacks therefore remain
 * recursive even during the stock finite flush's callback handoff. */
static bool risc_hcd_one_control(pipe_t *pipe)
{
    return pipe->ep_char.type == USB_PRIV_XFER_TYPE_CTRL && !pipe->ep_char.bEndpointAddress &&
        pipe->num_urb_pending + pipe->num_urb_done + NUM_BUFFERS -
            pipe->multi_buffer_control.buffer_num_to_fill <= 1;
}

esp_err_t risc_hcd_control_start(hcd_pipe_handle_t handle, urb_t *urb,
    hcd_pipe_callback_t callback, void *argument, hcd_pipe_callback_t expected, void *expected_arg,
    hcd_pipe_callback_t *previous, void **previous_arg)
{
    if (!handle || !urb || !callback || !expected || !previous || !previous_arg) return ESP_ERR_INVALID_ARG;
    if (!xPortEnterCriticalTimeout(&hcd_lock, 0)) return ESP_ERR_NOT_FINISHED;
    pipe_t *pipe = (pipe_t *)handle;
    esp_err_t ret = ESP_ERR_INVALID_STATE;
    if (!risc_hcd_one_control(pipe) || pipe->callback != expected || pipe->callback_arg != expected_arg ||
        pipe->bounded_dispatch || pipe->cs_flags.bounded_control || pipe->cs_flags.bounded_halt ||
        pipe->cs_flags.pipe_cmd_processing || pipe->cs_flags.reset_lock || pipe->cs_flags.has_urb ||
        pipe->task_waiting_pipe_notif || pipe->chan_obj->flags.active ||
        (pipe->ep_char.mps != 8 && pipe->ep_char.mps != 16 && pipe->ep_char.mps != 32 && pipe->ep_char.mps != 64) ||
        pipe->num_urb_pending || pipe->num_urb_done ||
        !TAILQ_EMPTY(&pipe->pending_urb_tailq) || !TAILQ_EMPTY(&pipe->done_urb_tailq) ||
        pipe->multi_buffer_control.buffer_num_to_fill != NUM_BUFFERS ||
        pipe->multi_buffer_control.buffer_num_to_exec || pipe->multi_buffer_control.buffer_num_to_parse ||
        pipe->multi_buffer_control.buffer_is_executing ||
        pipe->state != HCD_PIPE_STATE_ACTIVE || !pipe->port->flags.conn_dev_ena ||
        urb->transfer.num_isoc_packets || urb->transfer.bEndpointAddress ||
        urb->transfer.num_bytes < 8 || urb->transfer.num_bytes > 4104) goto exit;
    *previous = pipe->callback; *previous_arg = pipe->callback_arg;
    pipe->callback = callback; pipe->callback_arg = argument; pipe->cs_flags.bounded_control = 1;
    ret = hcd_urb_enqueue(handle, urb);
    if (ret != ESP_OK) {
        pipe->callback = *previous; pipe->callback_arg = *previous_arg;
        pipe->cs_flags.bounded_control = 0;
    }
exit:
    HCD_EXIT_CRITICAL(); return ret;
}

esp_err_t risc_hcd_control_cancel_step(hcd_pipe_handle_t handle)
{
    if (!xPortEnterCriticalTimeout(&hcd_lock, 0)) return ESP_ERR_NOT_FINISHED;
    pipe_t *pipe = (pipe_t *)handle;
    esp_err_t ret = ESP_ERR_NOT_FINISHED;
    if (!risc_hcd_one_control(pipe) || !pipe->cs_flags.bounded_control || pipe->cs_flags.reset_lock) goto exit;
    if (pipe->cs_flags.bounded_halt) {
        if (pipe->cs_flags.waiting_halt || pipe->state != HCD_PIPE_STATE_HALTED) goto exit;
        pipe->cs_flags.bounded_halt = 0; pipe->cs_flags.pipe_cmd_processing = 0;
    } else if (pipe->cs_flags.pipe_cmd_processing || pipe->task_waiting_pipe_notif) goto exit;
    if (pipe->state != HCD_PIPE_STATE_HALTED) {
        if (!pipe->port->flags.conn_dev_ena) goto exit;
        if (!usb_dwc_hal_chan_request_halt(pipe->chan_obj)) {
            pipe->cs_flags.waiting_halt = 1; pipe->cs_flags.bounded_halt = 1;
            pipe->cs_flags.pipe_cmd_processing = 1; goto exit;
        }
        usb_dwc_hal_chan_mark_halted(pipe->chan_obj); pipe->state = HCD_PIPE_STATE_HALTED;
    }
    ret = risc_hcd_flush_locked(pipe);
exit:
    HCD_EXIT_CRITICAL(); return ret;
}

esp_err_t risc_hcd_control_retire(hcd_pipe_handle_t handle, urb_t *expected)
{
    if (!xPortEnterCriticalTimeout(&hcd_lock, 0)) return ESP_ERR_NOT_FINISHED;
    pipe_t *pipe = (pipe_t *)handle;
    esp_err_t ret = ESP_ERR_INVALID_STATE;
    if (pipe->bounded_dispatch) { ret = ESP_ERR_NOT_FINISHED; goto exit; }
    if (!risc_hcd_one_control(pipe) || !pipe->cs_flags.bounded_control ||
        pipe->cs_flags.bounded_halt || pipe->cs_flags.pipe_cmd_processing || pipe->cs_flags.reset_lock ||
        pipe->task_waiting_pipe_notif || !pipe->cs_flags.has_urb || pipe->chan_obj->flags.active ||
        pipe->num_urb_pending || pipe->num_urb_done != 1 ||
        pipe->multi_buffer_control.buffer_num_to_fill != NUM_BUFFERS ||
        pipe->multi_buffer_control.buffer_num_to_exec || pipe->multi_buffer_control.buffer_num_to_parse ||
        pipe->multi_buffer_control.buffer_is_executing ||
        TAILQ_FIRST(&pipe->done_urb_tailq) != expected) goto exit;
    urb_t *retired = hcd_urb_dequeue(handle);
    if (retired == expected) ret = ESP_OK;
exit:
    HCD_EXIT_CRITICAL(); return ret;
}

esp_err_t risc_hcd_control_restore(hcd_pipe_handle_t handle, hcd_pipe_callback_t owner,
    void *owner_arg, hcd_pipe_callback_t previous, void *previous_arg)
{
    if (!xPortEnterCriticalTimeout(&hcd_lock, 0)) return ESP_ERR_NOT_FINISHED;
    pipe_t *pipe = (pipe_t *)handle;
    esp_err_t ret = ESP_ERR_INVALID_STATE;
    if (pipe->bounded_dispatch) { ret = ESP_ERR_NOT_FINISHED; goto exit; }
    if (!risc_hcd_one_control(pipe) || !pipe->cs_flags.bounded_control ||
        pipe->callback != owner || pipe->callback_arg != owner_arg ||
        pipe->cs_flags.bounded_halt || pipe->cs_flags.pipe_cmd_processing || pipe->cs_flags.reset_lock ||
        pipe->cs_flags.has_urb || pipe->task_waiting_pipe_notif || pipe->chan_obj->flags.active ||
        pipe->num_urb_pending || pipe->num_urb_done ||
        !TAILQ_EMPTY(&pipe->pending_urb_tailq) || !TAILQ_EMPTY(&pipe->done_urb_tailq) ||
        pipe->multi_buffer_control.buffer_num_to_fill != NUM_BUFFERS ||
        pipe->multi_buffer_control.buffer_num_to_exec || pipe->multi_buffer_control.buffer_num_to_parse ||
        pipe->multi_buffer_control.buffer_is_executing) goto exit;
    if (pipe->state == HCD_PIPE_STATE_HALTED) {
        ret = _pipe_cmd_clear(pipe);
        if (ret != ESP_OK) goto exit;
    }
    if (pipe->state != HCD_PIPE_STATE_ACTIVE) goto exit;
    pipe->callback = previous; pipe->callback_arg = previous_arg;
    pipe->cs_flags.bounded_control = 0; ret = ESP_OK;
exit:
    HCD_EXIT_CRITICAL(); return ret;
}
