#include "usb_control_rig.h"
void control_init(void) {
    admission_init();
    device.constant.default_pipe=&pipe;
    pipe.ep_char.type=USB_PRIV_XFER_TYPE_CTRL; pipe.ep_char.bEndpointAddress=0;
    pipe.callback=default_pipe_callback; pipe.callback_arg=&device; pipe.context=&device;
    TAILQ_INSERT_TAIL(&port.pipes_idle_tailq,&pipe,tailq_entry); port.num_pipes_idle=1;
    admission_hal.channels[0]=&channel;
    test_usbh.constant.notif_cb=control_notify;
    control_logs=control_notifications=control_hal_stage=control_hal_pid=0;
    control_hal_in=false;
}
void control_irq(unsigned event,int bytes) {
    assert(channel.flags.active);
    channel.event=event==0?USB_DWC_HAL_CHAN_EVENT_CPLT:USB_DWC_HAL_CHAN_EVENT_ERROR;
    channel.error=event==1?USB_DWC_HAL_CHAN_ERROR_XCS_XACT:event==2?USB_DWC_HAL_CHAN_ERROR_STALL:
                  event==3?USB_DWC_HAL_CHAN_ERROR_PKT_BBL:USB_DWC_HAL_CHAN_ERROR_BNA;
    dma_buffer_block_t *b=pipe.buffers[pipe.multi_buffer_control.rd_idx];
    if(b->flags.ctrl.cur_stg==1 && !event && !channel.flags.halt_requested) {
        assert(bytes>=0 && bytes<=request.transfer.num_bytes-8);
        ((usb_dwc_ll_dma_qtd_t *)b->xfer_desc_list)[1].remainder=request.transfer.num_bytes-8-bytes;
        if(b->flags.ctrl.data_stg_in) memset(data+8,0x6b,(size_t)bytes);
    }
    bool yield=false; HCD_ENTER_CRITICAL();
    hcd_pipe_event_t result=_intr_hdlr_chan(&pipe,&channel,&yield);
    if(result!=HCD_PIPE_EVENT_NONE) risc_hcd_dispatch_callback(&pipe,result,&yield);
    HCD_EXIT_CRITICAL();
}
void control_dispatch_hook(void (*hook)(unsigned)) { dispatch_hook=hook; }
void control_dispatch_decoded(unsigned event) {
    bool yield=false; HCD_ENTER_CRITICAL();
    risc_hcd_dispatch_callback(&pipe,event?HCD_PIPE_EVENT_ERROR_XFER:HCD_PIPE_EVENT_URB_DONE,&yield);
    HCD_EXIT_CRITICAL();
}
unsigned control_dispatch_count(void) { return pipe.bounded_dispatch; }
void control_dispatch_exhaust(void) { pipe.bounded_dispatch=UINT32_MAX-1; }
void control_pool_dispatch(uint64_t token,unsigned index) {
    endpoint_t *ep=admission_endpoint(token,index); pipe_t *p=ep->constant.pipe_hdl;
    bool yield=false; HCD_ENTER_CRITICAL();
    risc_hcd_dispatch_callback(p,HCD_PIPE_EVENT_URB_DONE,&yield);
    HCD_EXIT_CRITICAL();
}
void control_pool_drain(uint64_t token,unsigned index) {
    endpoint_t *ep=admission_endpoint(token,index); bool progressed=false;
    assert(risc_usb_host_client_step(&client,ep,&progressed)==ESP_OK && progressed);
}
void control_exhaust_cookies(void) { risc_control_serial=UINT32_MAX; }
unsigned control_stage(void) { return pipe.buffers[pipe.multi_buffer_control.rd_idx]->flags.ctrl.cur_stg; }
bool control_direction(void) { return control_hal_in; }
unsigned control_pid(void) { return control_hal_pid; }
unsigned control_count(void) { return device.dynamic.num_ctrl_xfers_inflight; }
unsigned control_logs_count(void) { return control_logs; }
unsigned control_notifications_count(void) { return control_notifications; }
bool control_callback_restored(void) { return !pipe.cs_flags.bounded_control && pipe.callback==default_pipe_callback && pipe.callback_arg==&device; }
bool control_retired(void) { return risc_control.retired; }
uint32_t control_cookie(void) { return risc_control.cookie; }
void control_saved_callback(uint32_t cookie) { risc_control_callback(&pipe,HCD_PIPE_EVENT_URB_DONE,(void *)(uintptr_t)cookie,true); }
void control_fault(unsigned fault,bool enabled) {
    switch(fault) {
    case 0:pipe.cs_flags.reset_lock=enabled;break;
    case 1:pipe.cs_flags.pipe_cmd_processing=enabled;break;
    case 2:pipe.task_waiting_pipe_notif=enabled?&pipe:NULL;break;
    case 3:pipe.callback=enabled?checked_pipe_callback:risc_control_callback;break;
    case 4:device.dynamic.num_ctrl_xfers_inflight=enabled?2:1;break;
    case 5:request.usb_host_inflight=!enabled;break;
    case 6:pipe.num_urb_pending=enabled;break;
    case 7:pipe.callback=enabled?checked_pipe_callback:default_pipe_callback;break;
    case 8:pipe.callback_arg=enabled?NULL:&device;break;
    case 9:device.dynamic.flags.in_pending_list=enabled;break;
    case 10:pipe.ep_char.type=enabled?USB_PRIV_XFER_TYPE_BULK:USB_PRIV_XFER_TYPE_CTRL;break;
    case 11:request.hcd_ptr=enabled?&pipe:NULL;break;
    case 12:pipe.multi_buffer_control.buffer_num_to_fill=enabled?1:NUM_BUFFERS;break;
    case 14:pipe.state=enabled?HCD_PIPE_STATE_HALTED:HCD_PIPE_STATE_ACTIVE;break;
    case 15:pipe.ep_char.mps=enabled?0:64;break;
    default:abort();
    }
}
esp_err_t control_legacy_submit(void) {
    urb_t other={0}; return usbh_dev_submit_ctrl_urb(&device,&other);
}
void control_legacy_notify(void) { pipe.callback(&pipe,HCD_PIPE_EVENT_ERROR_STALL,pipe.callback_arg,true); }
void control_legacy_updates(bool blocked) {
    esp_err_t expected=blocked?ESP_ERR_INVALID_STATE:ESP_OK;
    assert(hcd_pipe_update_mps(&pipe,64)==expected);
    assert(hcd_pipe_update_dev_addr(&pipe,1)==expected);
    assert(hcd_pipe_update_callback(&pipe,default_pipe_callback,&device)==expected);
    assert(hcd_pipe_set_persist_reset(&pipe)==expected);
}
