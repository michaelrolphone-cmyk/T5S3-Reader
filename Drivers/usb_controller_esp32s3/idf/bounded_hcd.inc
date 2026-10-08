/* SPDX-FileCopyrightText: 2015-2022 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 * Adaptation of ESP-IDF v4.4.7 hcd_dwc.c, appended by stage_usb_bounded.py.
 * Caller owns host_lock, so the stock pipe callback's HOST critical section
 * is recursive. Try HCD admission before calling any stock HCD helper; those
 * nested critical sections are then recursive too, never a cross-core wait.
 */
static void risc_hcd_dispatch_callback(pipe_t *pipe, hcd_pipe_event_t event, bool *yield)
{
    /* Called with the existing ISR HCD lock after event decode. Private
     * retirement must also account for a decoded callback which has not yet
     * returned. Save the pair before dropping the lock so no reused pool slot
     * or restored default pipe can receive an old event under its new cookie. */
    if (pipe->cs_flags.bounded_control || risc_hcd_pool_contains(pipe)) {
        hcd_pipe_callback_t callback = pipe->callback;
        void *argument = pipe->callback_arg;
        if (pipe->bounded_dispatch != UINT32_MAX) ++pipe->bounded_dispatch;
        HCD_EXIT_CRITICAL_ISR();
        *yield |= callback(pipe, event, argument, true);
        HCD_ENTER_CRITICAL_ISR();
        /* Overflow is terminal custody, never a wrap back to idle. */
        if (pipe->bounded_dispatch != UINT32_MAX) --pipe->bounded_dispatch;
    } else {
        HCD_EXIT_CRITICAL_ISR();
        *yield |= pipe->callback(pipe, event, pipe->callback_arg, true);
        HCD_ENTER_CRITICAL_ISR();
    }
}

static bool risc_hcd_one_bulk(pipe_t *pipe)
{
    return pipe->ep_char.type == USB_PRIV_XFER_TYPE_BULK &&
           pipe->num_urb_pending + pipe->num_urb_done + NUM_BUFFERS -
               pipe->multi_buffer_control.buffer_num_to_fill <= 1;
}

static esp_err_t risc_hcd_flush_locked(pipe_t *pipe)
{
    /* _pipe_cmd_flush drops/re-enters one HCD critical level around its
     * callback. Keep the already-acquired outer level so that re-entry cannot
     * wait for another core. The caller holds host_lock too, making the
     * controller's stock pipe notification callback recursive and bounded. */
    HCD_ENTER_CRITICAL();
    esp_err_t ret = _pipe_cmd_flush(pipe);
    HCD_EXIT_CRITICAL();
    return ret;
}

esp_err_t risc_hcd_bulk_enqueue(hcd_pipe_handle_t handle, urb_t *urb)
{
    if (!xPortEnterCriticalTimeout(&hcd_lock, 0)) return ESP_ERR_NOT_FINISHED;
    pipe_t *pipe = (pipe_t *)handle;
    esp_err_t ret = ESP_ERR_INVALID_STATE;
    if (risc_hcd_one_bulk(pipe) && !pipe->cs_flags.has_urb &&
        !pipe->cs_flags.bounded_halt && urb->transfer.num_isoc_packets == 0)
        ret = hcd_urb_enqueue(handle, urb);
    HCD_EXIT_CRITICAL();
    return ret;
}

esp_err_t risc_hcd_dequeue_step(hcd_pipe_handle_t handle, bool flush, urb_t **out, bool *empty)
{
    *out = NULL;
    *empty = false;
    if (!xPortEnterCriticalTimeout(&hcd_lock, 0)) return ESP_ERR_NOT_FINISHED;
    pipe_t *pipe = (pipe_t *)handle;
    esp_err_t ret = ESP_OK;
    if (flush) {
        /* The stock flush has a pending-URB loop and per-ISO packet loop.
         * Restrict this path to one non-ISO bulk URB before entering it. */
        if (!risc_hcd_one_bulk(pipe) || pipe->cs_flags.bounded_halt ||
            pipe->cs_flags.pipe_cmd_processing || pipe->cs_flags.reset_lock)
            ret = ESP_ERR_NOT_FINISHED;
        else
            ret = risc_hcd_flush_locked(pipe);
    }
    if (ret == ESP_OK) {
        *out = hcd_urb_dequeue(handle);
        *empty = pipe->num_urb_done == 0;
    }
    HCD_EXIT_CRITICAL();
    return ret;
}

esp_err_t risc_hcd_bulk_cancel_step(hcd_pipe_handle_t handle)
{
    if (!xPortEnterCriticalTimeout(&hcd_lock, 0)) return ESP_ERR_NOT_FINISHED;
    pipe_t *pipe = (pipe_t *)handle;
    esp_err_t ret = ESP_ERR_NOT_FINISHED;
    if (!risc_hcd_one_bulk(pipe) || pipe->cs_flags.reset_lock) goto exit;
    if (pipe->cs_flags.bounded_halt) {
        /* Only the real ISR can finish an outstanding hardware halt. A
         * disconnect, task wake, or caller deadline is not that evidence. */
        if (pipe->cs_flags.waiting_halt) goto exit;
        if (pipe->state != HCD_PIPE_STATE_HALTED) goto exit;
        pipe->cs_flags.bounded_halt = 0;
        pipe->cs_flags.pipe_cmd_processing = 0;
    } else if (pipe->cs_flags.pipe_cmd_processing ||
               pipe->task_waiting_pipe_notif != NULL) {
        goto exit;
    }
    if (pipe->state != HCD_PIPE_STATE_HALTED) {
        if (!pipe->port->flags.conn_dev_ena) goto exit;
        if (!usb_dwc_hal_chan_request_halt(pipe->chan_obj)) {
            pipe->cs_flags.waiting_halt = 1;
            pipe->cs_flags.bounded_halt = 1;
            pipe->cs_flags.pipe_cmd_processing = 1;
            goto exit;
        }
        /* The HAL reported an already halted channel. */
        usb_dwc_hal_chan_mark_halted(pipe->chan_obj);
        pipe->state = HCD_PIPE_STATE_HALTED;
    }
    ret = risc_hcd_flush_locked(pipe);
exit:
    HCD_EXIT_CRITICAL();
    return ret;
}

esp_err_t risc_hcd_bulk_clear(hcd_pipe_handle_t handle)
{
    if (!xPortEnterCriticalTimeout(&hcd_lock, 0)) return ESP_ERR_NOT_FINISHED;
    pipe_t *pipe = (pipe_t *)handle;
    esp_err_t ret = ESP_ERR_NOT_FINISHED;
    if (risc_hcd_one_bulk(pipe) && !pipe->cs_flags.bounded_halt &&
        !pipe->cs_flags.pipe_cmd_processing && !pipe->cs_flags.reset_lock &&
        !pipe->cs_flags.has_urb)
        ret = _pipe_cmd_clear(pipe);
    HCD_EXIT_CRITICAL();
    return ret;
}
