#include "usb_admission_rig.h"
#undef interface
static fake_hal_t admission_hal;
static const fifo_mps_limits_t admission_fifo={512,512,512};
static usb_device_desc_t admission_descriptor={0x1234,0x5678};
static uint8_t admission_bytes[4096];

void admission_config(unsigned interfaces,unsigned endpoints) {
    assert(interfaces<=16 && endpoints<=8);
    memset(admission_bytes,0,sizeof(admission_bytes));
    size_t length=9+interfaces*(9+endpoints*7);
    const uint8_t header[]={9,2,(uint8_t)length,(uint8_t)(length>>8),(uint8_t)interfaces,1,0,0x80,50};
    memcpy(admission_bytes,header,sizeof(header));
    size_t offset=9;
    for(unsigned n=0;n<interfaces;++n) {
        uint8_t intf[]={9,4,(uint8_t)n,0,(uint8_t)endpoints,0xff,0,0,0};
        memcpy(admission_bytes+offset,intf,9); offset+=9;
        for(unsigned ep=0;ep<endpoints;++ep) {
            /* Endpoint addresses are unique across interfaces in each fixture. */
            uint8_t address=(uint8_t)(1+(n*endpoints+ep)%15);
            if((n*endpoints+ep)&1) address|=0x80;
            uint8_t desc[]={7,5,address,USB_TRANSFER_TYPE_BULK,64,0,1};
            memcpy(admission_bytes+offset,desc,7); offset+=7;
        }
    }
    device.constant.config_desc=(const usb_config_desc_t *)admission_bytes;
}
void admission_init(void) {
    rig_reset();
    memset(&admission_hal,0,sizeof(admission_hal));
    admission_hal.occupied=1; /* The existing enumerated device owns EP0. */
    TAILQ_INIT(&client.dynamic.idle_ep_tailq); TAILQ_INIT(&client.dynamic.pending_ep_tailq);
    TAILQ_INIT(&client.mux_protected.interface_tailq);
    TAILQ_INIT(&port.pipes_idle_tailq); TAILQ_INIT(&port.pipes_active_tailq);
    port.num_pipes_idle=port.num_pipes_queued=0; port.initialized=true;
    port.hal=&admission_hal; port.fifo_mps_limits=&admission_fifo; port.speed=USB_SPEED_FULL;
    memset(&device,0,sizeof(device)); device.dynamic.ref_count=1;
    device.dynamic.state=USB_DEVICE_STATE_CONFIGURED;
    device.constant.address=1; device.constant.speed=USB_SPEED_FULL;
    device.constant.desc=&admission_descriptor; device.constant.port_hdl=&port;
    client.dynamic.opened_dev_addr_map=1;
    TAILQ_INIT(&test_usbh.dynamic.devs_idle_tailq); TAILQ_INIT(&test_usbh.dynamic.devs_pending_tailq);
    TAILQ_INSERT_TAIL(&test_usbh.dynamic.devs_idle_tailq,&device,dynamic.tailq_entry);
    admission_config(2,2);
}
esp_err_t admission_prepare(unsigned fail_at) {
    assert(memory_forbidden); allocations=frees=0; fail_allocation=fail_at;
    memory_forbidden=false; esp_err_t rc=risc_usb_admission_prepare(); memory_forbidden=true;
    return rc;
}
esp_err_t admission_dispose(void) {
    memory_forbidden=false; esp_err_t rc=risc_usb_admission_dispose(); memory_forbidden=true;
    return rc;
}
unsigned admission_allocations(void) { return allocations; }
unsigned admission_live_allocations(void) { return live_allocations; }
unsigned admission_channels(void) { return admission_hal.allocated; }
void admission_channel_occupancy(unsigned mask) { admission_hal.occupied=mask; }
void admission_config_corrupt(unsigned offset,unsigned value) { assert(offset<4096); admission_bytes[offset]=(uint8_t)value; }
static endpoint_t *admission_endpoint(uint64_t token,unsigned index) {
    risc_native_claim *claim=risc_native_find(token);
    assert(claim && index<claim->acquired);
    return claim->interface->constant.endpoints[index];
}
void admission_hcd_fault(uint64_t token,unsigned index,unsigned fault,bool enabled) {
    pipe_t *p=(pipe_t *)admission_endpoint(token,index)->constant.pipe_hdl;
    switch(fault) {
    case 0:p->cs_flags.bounded_halt=enabled;break;
    case 1:p->cs_flags.pipe_cmd_processing=enabled;break;
    case 2:p->cs_flags.has_urb=enabled;break;
    case 3:p->cs_flags.reset_lock=enabled;break;
    case 4:p->task_waiting_pipe_notif=enabled?p:NULL;break;
    case 5:p->chan_obj->flags.active=enabled;break;
    case 6:p->multi_buffer_control.buffer_is_executing=enabled;break;
    case 7:p->num_urb_pending=enabled;break;
    case 8:p->num_urb_done=enabled;break;
    case 9:p->multi_buffer_control.buffer_num_to_exec=enabled;break;
    case 10:p->multi_buffer_control.buffer_num_to_parse=enabled;break;
    default:abort();
    }
}
void admission_host_fault(uint64_t token,unsigned index,unsigned fault,bool enabled) {
    endpoint_t *ep=admission_endpoint(token,index);
    if(fault==0) ep->dynamic.num_urb_inflight=enabled;
    else if(fault==1) ep->dynamic.flags.pending=enabled;
    else if(fault==2) {
        unsigned address=ep->constant.ep_desc->bEndpointAddress&15u;
        hcd_pipe_handle_t *map=(ep->constant.ep_desc->bEndpointAddress&0x80u)?
            &device.mux_protected.ep_in[address-1]:&device.mux_protected.ep_out[address-1];
        *map=enabled?NULL:ep->constant.pipe_hdl;
    } else abort();
}
void admission_device_state(unsigned state) {
    if(state==1) { device.dynamic.flags.is_gone=1; device.dynamic.state=USB_DEVICE_STATE_NOT_ATTACHED; }
    if(state==2) device.dynamic.flags.waiting_close=1;
    if(state==3) { (void)_dev_set_actions(&device,0x10); }
    if(state==4) device.dynamic.num_ctrl_xfers_inflight=1;
    if(state==5) device.dynamic.ref_count=0;
    if(state==6) device.dynamic.flags.waiting_free=1;
    if(state==7) client.dynamic.opened_dev_addr_map=0;
}
unsigned admission_device_refs(void) { return device.dynamic.ref_count; }
unsigned admission_device_actions(void) { return device.dynamic.action_flags; }
bool admission_device_opened(void) { return client.dynamic.opened_dev_addr_map!=0; }
uint32_t admission_endpoint_cookie(uint64_t token,unsigned index) {
    endpoint_t *ep=admission_endpoint(token,index);
    return __containerof(ep,risc_native_endpoint,endpoint)->cookie;
}
void admission_endpoint_callback(uint64_t token,unsigned index,bool stale) {
    endpoint_t *ep=admission_endpoint(token,index); pipe_t *p=(pipe_t *)ep->constant.pipe_hdl;
    uint32_t cookie=admission_endpoint_cookie(token,index);
    if(stale) cookie+=8;
    p->callback(p,HCD_PIPE_EVENT_URB_DONE,(void *)(uintptr_t)cookie,true);
}
void admission_saved_callback(uint32_t cookie) {
    unsigned index=cookie&7u;
    risc_native_endpoint *slot=&risc_native_endpoints[index];
    risc_native_pipe_callback(slot->endpoint.constant.pipe_hdl,HCD_PIPE_EVENT_URB_DONE,(void *)(uintptr_t)cookie,true);
}
esp_err_t admission_legacy_release(uint64_t token) {
    risc_native_claim *claim=risc_native_find(token);
    assert(claim && claim->published);
    return interface_release(&client,&device,claim->number);
}
esp_err_t admission_legacy_pipe_free(uint64_t token,unsigned index) {
    return hcd_pipe_free(admission_endpoint(token,index)->constant.pipe_hdl);
}
void admission_bulk_irq(uint64_t token,unsigned index,unsigned event,int bytes) {
    endpoint_t *ep=admission_endpoint(token,index); pipe_t *p=(pipe_t *)ep->constant.pipe_hdl;
    assert(p->chan_obj->flags.active);
    p->chan_obj->event=event==1?USB_DWC_HAL_CHAN_EVENT_CPLT:USB_DWC_HAL_CHAN_EVENT_ERROR;
    p->chan_obj->error=USB_DWC_HAL_CHAN_ERROR_STALL;
    dma_buffer_block_t *buffer=p->buffers[p->multi_buffer_control.rd_idx];
    ((usb_dwc_ll_dma_qtd_t *)buffer->xfer_desc_list)[0].remainder=buffer->urb->transfer.num_bytes-bytes;
    bool yield=false; HCD_ENTER_CRITICAL();
    hcd_pipe_event_t result=_intr_hdlr_chan(p,p->chan_obj,&yield);
    if(result!=HCD_PIPE_EVENT_NONE) risc_hcd_dispatch_callback(p,result,&yield);
    HCD_EXIT_CRITICAL();
}
