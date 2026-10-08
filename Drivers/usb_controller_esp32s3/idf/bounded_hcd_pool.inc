/* SPDX-FileCopyrightText: 2015-2022 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 * Fixed resources for native bulk/interrupt claims. Preparation/disposal are
 * explicitly OUTSIDE the deadline contract. Allocation and release below are
 * bounded native steps; neither reaches an allocator, semaphore or logger. */
enum { RISC_HCD_POOL_COUNT = USB_DWC_HAL_NUM_CHAN,
       RISC_HCD_QTD_STRIDE = USB_DWC_HAL_DMA_MEM_ALIGN };
_Static_assert(XFER_LIST_LEN_INTR * sizeof(usb_dwc_ll_dma_qtd_t) <= RISC_HCD_QTD_STRIDE,
               "Pinned periodic descriptor list must fit one aligned pool stride");
typedef struct {
    pipe_t pipe;
    usb_dwc_hal_chan_t channel;
    dma_buffer_block_t buffers[NUM_BUFFERS];
    bool used;
} risc_hcd_pool_slot_t;
static risc_hcd_pool_slot_t *risc_hcd_slots;
static uint8_t *risc_hcd_dma;
__attribute__((used)) static const size_t risc_hcd_pool_cost[] = {
    sizeof(risc_hcd_pool_slot_t) * RISC_HCD_POOL_COUNT,
    RISC_HCD_POOL_COUNT * NUM_BUFFERS * RISC_HCD_QTD_STRIDE
};

size_t risc_hcd_pool_internal_bytes(void) { return sizeof(risc_hcd_pool_slot_t) * RISC_HCD_POOL_COUNT; }
size_t risc_hcd_pool_dma_bytes(void) { return RISC_HCD_POOL_COUNT * NUM_BUFFERS * RISC_HCD_QTD_STRIDE; }

esp_err_t risc_hcd_pool_prepare(void)
{
    if (risc_hcd_slots) return ESP_OK;
    risc_hcd_pool_slot_t *slots = heap_caps_calloc(RISC_HCD_POOL_COUNT,
        sizeof(risc_hcd_pool_slot_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uint8_t *dma = heap_caps_aligned_calloc(USB_DWC_HAL_DMA_MEM_ALIGN,
        risc_hcd_pool_dma_bytes(), 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!slots || !dma) {
        heap_caps_free(slots); heap_caps_free(dma);
        return ESP_ERR_NO_MEM;
    }
    /* Only explicit preparation performs allocation. No physical channel is
     * claimed here, and no deadline operation calls this function implicitly. */
    risc_hcd_slots = slots; risc_hcd_dma = dma;
    return ESP_OK;
}

static bool risc_hcd_pool_contains(hcd_pipe_handle_t handle)
{
    if (!risc_hcd_slots) return false;
    for (unsigned i = 0; i < RISC_HCD_POOL_COUNT; ++i)
        if (&risc_hcd_slots[i].pipe == (pipe_t *)handle) return true;
    return false;
}

esp_err_t risc_hcd_pool_dispose(void)
{
    if (!risc_hcd_slots) return ESP_OK;
    if (!xPortEnterCriticalTimeout(&hcd_lock, 0)) return ESP_ERR_NOT_FINISHED;
    for (unsigned i = 0; i < RISC_HCD_POOL_COUNT; ++i) {
        if (risc_hcd_slots[i].used || risc_hcd_slots[i].pipe.bounded_dispatch) { HCD_EXIT_CRITICAL(); return ESP_ERR_INVALID_STATE; }
    }
    risc_hcd_pool_slot_t *slots = risc_hcd_slots;
    uint8_t *dma = risc_hcd_dma;
    risc_hcd_slots = NULL; risc_hcd_dma = NULL;
    HCD_EXIT_CRITICAL();
    heap_caps_free(dma); heap_caps_free(slots);
    return ESP_OK;
}

esp_err_t risc_hcd_pool_allocate(hcd_port_handle_t handle, const hcd_pipe_config_t *config,
                                hcd_pipe_handle_t *out)
{
    if (!handle || !config || !config->ep_desc || !config->callback || !out)
        return ESP_ERR_INVALID_ARG;
    if (!risc_hcd_slots) return ESP_ERR_INVALID_STATE;
    if (!xPortEnterCriticalTimeout(&hcd_lock, 0)) return ESP_ERR_NOT_FINISHED;
    port_t *port = (port_t *)handle;
    const usb_ep_desc_t *ep = config->ep_desc;
    usb_transfer_type_t type = USB_EP_DESC_GET_XFERTYPE(ep);
    esp_err_t ret = ESP_ERR_NOT_SUPPORTED;
    if (!port->initialized || !port->flags.conn_dev_ena ||
        port->state != HCD_PORT_STATE_ENABLED) { ret = ESP_ERR_INVALID_STATE; goto exit; }
    /* Same supported bulk/interrupt/FIFO constraints as the pinned helpers,
     * with explicit argument bounds and no ESP_LOGE allocation/OS path. */
    if ((type != USB_TRANSFER_TYPE_BULK && type != USB_TRANSFER_TYPE_INTR) ||
        !(ep->bEndpointAddress & 0x0fu) || (ep->bEndpointAddress & 0x70u) ||
        !ep->wMaxPacketSize || (ep->wMaxPacketSize & 0xf800u) ||
        (port->speed == USB_SPEED_LOW && config->dev_speed == USB_SPEED_FULL) ||
        (config->dev_speed == USB_SPEED_LOW && type == USB_TRANSFER_TYPE_BULK) ||
        (type == USB_TRANSFER_TYPE_INTR && (!ep->bInterval || ep->bInterval > FRAME_LIST_LEN))) goto exit;
    int limit = USB_EP_DESC_GET_EP_DIR(ep) ? port->fifo_mps_limits->in_mps :
        (type == USB_TRANSFER_TYPE_BULK ? port->fifo_mps_limits->non_periodic_out_mps :
                                       port->fifo_mps_limits->periodic_out_mps);
    if (ep->wMaxPacketSize > limit) goto exit;
    risc_hcd_pool_slot_t *slot = NULL;
    unsigned index = 0;
    for (; index < RISC_HCD_POOL_COUNT; ++index) {
        if (!risc_hcd_slots[index].used) { slot = &risc_hcd_slots[index]; break; }
    }
    if (!slot) { ret = ESP_ERR_NO_MEM; goto exit; }
    memset(slot, 0, sizeof(*slot));
    pipe_t *pipe = &slot->pipe;
    TAILQ_INIT(&pipe->pending_urb_tailq); TAILQ_INIT(&pipe->done_urb_tailq);
    for (unsigned i = 0; i < NUM_BUFFERS; ++i) {
        slot->buffers[i].xfer_desc_list = risc_hcd_dma +
            (index * NUM_BUFFERS + i) * RISC_HCD_QTD_STRIDE;
        memset(slot->buffers[i].xfer_desc_list, 0, RISC_HCD_QTD_STRIDE);
        pipe->buffers[i] = &slot->buffers[i];
    }
    pipe->multi_buffer_control.buffer_num_to_fill = NUM_BUFFERS;
    pipe->port = port; pipe->chan_obj = &slot->channel;
    pipe_set_ep_char(config, type, false, port->num_pipes_idle + port->num_pipes_queued,
                     port->speed, &pipe->ep_char);
    pipe->state = HCD_PIPE_STATE_ACTIVE;
    pipe->callback = config->callback; pipe->callback_arg = config->callback_arg;
    pipe->context = config->context;
    /* Real HAL allocation respects already occupied EP0/legacy channels. */
    if (!usb_dwc_hal_chan_alloc(port->hal, pipe->chan_obj, pipe)) {
        ret = ESP_ERR_NO_MEM; goto exit;
    }
    usb_dwc_hal_chan_set_ep_char(port->hal, pipe->chan_obj, &pipe->ep_char);
    TAILQ_INSERT_TAIL(&port->pipes_idle_tailq, pipe, tailq_entry);
    port->num_pipes_idle++; slot->used = true; *out = pipe;
    ret = ESP_OK;
exit:
    HCD_EXIT_CRITICAL();
    return ret;
}

esp_err_t risc_hcd_pool_release(hcd_pipe_handle_t handle)
{
    if (!xPortEnterCriticalTimeout(&hcd_lock, 0)) return ESP_ERR_NOT_FINISHED;
    risc_hcd_pool_slot_t *slot = NULL;
    if (risc_hcd_slots) for (unsigned i = 0; i < RISC_HCD_POOL_COUNT; ++i)
        if (&risc_hcd_slots[i].pipe == (pipe_t *)handle) { slot = &risc_hcd_slots[i]; break; }
    esp_err_t ret = ESP_ERR_INVALID_STATE;
    if (!slot || !slot->used) goto exit;
    pipe_t *pipe = &slot->pipe;
    if (pipe->bounded_dispatch) { ret = ESP_ERR_NOT_FINISHED; goto exit; }
    if (pipe->cs_flags.bounded_halt || pipe->cs_flags.pipe_cmd_processing ||
        pipe->cs_flags.has_urb || pipe->cs_flags.reset_lock || pipe->task_waiting_pipe_notif ||
        pipe->chan_obj->flags.active || pipe->multi_buffer_control.buffer_is_executing ||
        pipe->num_urb_pending || pipe->num_urb_done ||
        pipe->multi_buffer_control.buffer_num_to_exec || pipe->multi_buffer_control.buffer_num_to_parse)
        goto exit;
    usb_dwc_hal_chan_free(pipe->port->hal, pipe->chan_obj);
    TAILQ_REMOVE(&pipe->port->pipes_idle_tailq, pipe, tailq_entry);
    pipe->port->num_pipes_idle--;
    slot->used = false; /* DMA remains allocated and owned by the explicit pool. */
    ret = ESP_OK;
exit:
    HCD_EXIT_CRITICAL();
    return ret;
}
