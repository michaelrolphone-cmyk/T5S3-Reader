/* Only buffer submission/execution is simulated. Retirement, ISR state,
 * callback dispatch, cancellation, flush and clear run the staged SDK bodies. */
struct port_obj {
    TAILQ_HEAD(idle_pipes, pipe_obj) pipes_idle_tailq;
    TAILQ_HEAD(active_pipes, pipe_obj) pipes_active_tailq;
    int num_pipes_idle, num_pipes_queued, state;
    struct { bool conn_dev_ena; } flags;
    void *hal;
    bool initialized;
    usb_speed_t speed;
    const fifo_mps_limits_t *fifo_mps_limits;
};
#ifndef RISC_USB_ADMISSION_TEST
typedef struct { struct { hcd_pipe_handle_t ep_in[15], ep_out[15]; } mux_protected; } device_t;
static bool risc_hcd_pool_contains(hcd_pipe_handle_t handle) { (void)handle; return false; }
#else
static bool risc_hcd_pool_contains(hcd_pipe_handle_t handle);
#endif
static bool _unblock_client(client_t *client, bool isr) {
    assert(!check_flush_lock); /* Owned notifications must skip every OS call. */
    (void)isr; client->dynamic.flags.events_pending=1; return false;
}
static bool _internal_pipe_event_notify(pipe_t *pipe,bool isr) {
    (void)pipe; (void)isr; abort(); /* No task wait is allowed on the staged path. */
}
#ifdef RISC_USB_CONTROL_TEST
static inline void _buffer_parse_ctrl(dma_buffer_block_t *buffer);
static inline void _buffer_fill_ctrl(dma_buffer_block_t *buffer,usb_transfer_t *transfer);
static void _buffer_exec_cont(pipe_t *pipe);
#else
static void _buffer_parse_ctrl(dma_buffer_block_t *buffer) { (void)buffer; abort(); }
static void _buffer_exec_cont(pipe_t *pipe) { (void)pipe; abort(); }
#endif
static void _buffer_parse_intr(dma_buffer_block_t *buffer,bool in,int mps) { (void)buffer;(void)in;(void)mps;abort(); }
static void _buffer_parse_isoc(dma_buffer_block_t *buffer,bool in) { (void)buffer;(void)in;abort(); }
static void _buffer_fill(pipe_t *pipe) {
    assert((pipe->ep_char.type==USB_PRIV_XFER_TYPE_BULK || pipe->ep_char.type==USB_PRIV_XFER_TYPE_CTRL) && pipe->num_urb_pending==1);
    urb_t *urb=TAILQ_FIRST(&pipe->pending_urb_tailq);
    TAILQ_REMOVE(&pipe->pending_urb_tailq,urb,tailq_entry);
    --pipe->num_urb_pending;
    dma_buffer_block_t *buffer=pipe->buffers[pipe->multi_buffer_control.wr_idx];
    buffer->urb=urb; urb->hcd_var=URB_HCD_STATE_INFLIGHT;
#ifdef RISC_USB_CONTROL_TEST
    if(pipe->ep_char.type==USB_PRIV_XFER_TYPE_CTRL) _buffer_fill_ctrl(buffer,&urb->transfer);
#endif
    ++pipe->multi_buffer_control.wr_idx;
    pipe->multi_buffer_control.wr_idx%=NUM_BUFFERS;
    --pipe->multi_buffer_control.buffer_num_to_fill;
    ++pipe->multi_buffer_control.buffer_num_to_exec;
}
static void _buffer_exec(pipe_t *pipe) {
    pipe->multi_buffer_control.buffer_is_executing=1;
    pipe->buffers[pipe->multi_buffer_control.rd_idx]->status_flags.executing=1;
    pipe->chan_obj->flags.active=true;
}
static esp_err_t _pipe_cmd_halt(pipe_t *pipe) { (void)pipe; abort(); }
static void buffer_block_free(dma_buffer_block_t *buffer) { (void)buffer; abort(); }
