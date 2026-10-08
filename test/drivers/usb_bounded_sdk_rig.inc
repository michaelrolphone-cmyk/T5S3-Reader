static host_lib_t library;
static client_t client;
static endpoint_t endpoint;
static interface_t *interface_pointer;
#define interface (*interface_pointer)
static device_t device;
static pipe_t pipe;
static port_t port;
static usb_dwc_hal_chan_t channel;
static urb_t request;
static fake_queue_t queue;
static usb_ep_desc_t descriptor;
static usb_intf_desc_t intf_descriptor;
static dma_buffer_block_t buffers[NUM_BUFFERS];
static usb_dwc_ll_dma_qtd_t descriptors[NUM_BUFFERS][XFER_LIST_LEN_CTRL];
static uint8_t data[4104];
static bool checked_pipe_callback(hcd_pipe_handle_t handle,hcd_pipe_event_t event,void *arg,bool isr) {
    if(check_flush_lock) {
        assert(!isr && hcd_lock.depth>0 && host_lock.depth>0);
        ++checked_flush_callbacks;
    }
    return pipe_callback(handle,event,arg,isr);
}
static void message_callback(const usb_host_client_event_msg_t *message,void *arg) {
    (void)message; (void)arg; ++messages;
    if(replenish_events) ++queue.count;
}
void rig_reset(void) {
    free(interface_pointer);
    /* Match the SDK's actual trailing endpoint-pointer allocation. */
    interface_pointer=calloc(1,sizeof(interface_t)+sizeof(endpoint_t *));
    assert(interface_pointer);
    memset(&library,0,sizeof(library)); memset(&client,0,sizeof(client));
    memset(&endpoint,0,sizeof(endpoint)); memset(&interface,0,sizeof(interface));
    memset(&device,0,sizeof(device)); memset(&pipe,0,sizeof(pipe));
    memset(&port,0,sizeof(port)); memset(&channel,0,sizeof(channel));
    memset(&request,0,sizeof(request)); memset(&queue,0,sizeof(queue));
    memset(buffers,0,sizeof(buffers)); memset(descriptors,0,sizeof(descriptors));
    memset(data,0,sizeof(data));
    host_lock=(portMUX_TYPE){0}; hcd_lock=(portMUX_TYPE){0}; usbh_lock=(portMUX_TYPE){0};
    halt_requests=messages=0; replenish_events=false;
    check_flush_lock=false; checked_flush_callbacks=0;
    p_host_lib_obj=&library; library.dynamic.flags.num_clients=1;
    library.constant.mux_lock=&usbh_lock;
    TAILQ_INIT(&library.mux_protected.client_tailq);
    TAILQ_INSERT_TAIL(&library.mux_protected.client_tailq,&client,dynamic.tailq_entry);
    TAILQ_INIT(&client.dynamic.pending_ep_tailq); TAILQ_INIT(&client.dynamic.idle_ep_tailq);
    TAILQ_INIT(&client.dynamic.done_ctrl_xfer_tailq); TAILQ_INIT(&client.mux_protected.interface_tailq);
    client.constant.event_callback=message_callback; client.constant.event_msg_queue=&queue;
    queue.item.event=USB_HOST_CLIENT_EVENT_NEW_DEV;
    TAILQ_INSERT_TAIL(&client.dynamic.idle_ep_tailq,&endpoint,dynamic.tailq_entry);
    descriptor=(usb_ep_desc_t){.bEndpointAddress=0x81,.bmAttributes=USB_TRANSFER_TYPE_BULK,.wMaxPacketSize=64};
    intf_descriptor=(usb_intf_desc_t){.bInterfaceNumber=2,.bAlternateSetting=0,.bNumEndpoints=1};
    interface.constant.intf_desc=&intf_descriptor; interface.constant.client_obj=&client;
    interface.constant.dev_hdl=&device;
    interface.constant.endpoints[0]=&endpoint;
    TAILQ_INSERT_TAIL(&client.mux_protected.interface_tailq,&interface,mux_protected.tailq_entry);
    endpoint.constant.intf_obj=&interface; endpoint.constant.ep_desc=&descriptor;
    endpoint.constant.pipe_hdl=&pipe;
    device.mux_protected.ep_in[0]=&pipe; device.mux_protected.ep_out[0]=&pipe;
    pipe.context=&endpoint; pipe.chan_obj=&channel; pipe.port=&port;
    pipe.ep_char=(usb_dwc_hal_ep_char_t){.type=USB_PRIV_XFER_TYPE_BULK,.mps=64,.bEndpointAddress=0x81};
    pipe.callback=checked_pipe_callback; pipe.callback_arg=&endpoint;
    pipe.state=HCD_PIPE_STATE_ACTIVE; pipe.multi_buffer_control.buffer_num_to_fill=NUM_BUFFERS;
    TAILQ_INIT(&pipe.pending_urb_tailq); TAILQ_INIT(&pipe.done_urb_tailq);
    TAILQ_INIT(&port.pipes_idle_tailq); TAILQ_INIT(&port.pipes_active_tailq);
    TAILQ_INSERT_TAIL(&port.pipes_idle_tailq,&pipe,tailq_entry); port.num_pipes_idle=1;
    port.flags.conn_dev_ena=true; port.state=HCD_PORT_STATE_ENABLED;
    for(int i=0;i<NUM_BUFFERS;++i) { pipe.buffers[i]=&buffers[i]; buffers[i].xfer_desc_list=descriptors[i]; }
    request.transfer.data_buffer=data; request.transfer.data_buffer_size=sizeof(data);
}
usb_transfer_t *rig_transfer(void) { return &request.transfer; }
usb_device_handle_t rig_device(void) { return &device; }
usb_host_client_handle_t rig_client(void) { return &client; }
void rig_lock(unsigned lock,bool blocked) {
    portMUX_TYPE *locks[]={&host_lock,&usbh_lock,&hcd_lock}; assert(lock<3); locks[lock]->blocked=blocked;
}
void rig_library_pending(unsigned flags) { library.dynamic.process_pending_flags=flags; library.dynamic.flags.process_pending=flags!=0; }
void rig_library_missing(void) { p_host_lib_obj=NULL; }
unsigned rig_library_flags(void) { return library.dynamic.process_pending_flags; }
void rig_irq(unsigned event,int bytes) {
    assert(channel.flags.active);
    channel.event=event==1?USB_DWC_HAL_CHAN_EVENT_CPLT:USB_DWC_HAL_CHAN_EVENT_ERROR;
    channel.error=USB_DWC_HAL_CHAN_ERROR_STALL;
    descriptors[pipe.multi_buffer_control.rd_idx][0].remainder=request.transfer.num_bytes-bytes;
    memset(data,0x5a,(size_t)(bytes<0?0:bytes));
    bool yield=false;
    HCD_ENTER_CRITICAL();
    hcd_pipe_event_t result=_intr_hdlr_chan(&pipe,&channel,&yield);
    if(result!=HCD_PIPE_EVENT_NONE) risc_hcd_dispatch_callback(&pipe,result,&yield);
    HCD_EXIT_CRITICAL();
}
void rig_disconnect(void) { port.flags.conn_dev_ena=false; rig_library_pending(3); }
void rig_queue_events(unsigned count,bool replenish) { queue.count=count; client.dynamic.bounded_num_messages=count; replenish_events=replenish; client.dynamic.flags.events_pending=1; }
unsigned rig_events(void) { return messages; }
unsigned rig_halts(void) { return halt_requests; }
bool rig_dma_active(void) { return channel.flags.active || pipe.multi_buffer_control.buffer_is_executing; }
bool rig_halt_owned(void) { return pipe.cs_flags.bounded_halt; }
bool rig_callback_custody(void) { return request.usb_host_inflight; }
void rig_extra_pending(unsigned count) { pipe.num_urb_pending=(int)count; }
void rig_pending_transfer(void) {
    /* Model an admitted HCD URB waiting for execution, exercising the stock
     * pending-queue flush and callback branch, not a synthetic completion. */
    assert(request.usb_host_inflight && pipe.num_urb_pending==0);
    for(int i=0;i<NUM_BUFFERS;++i) { buffers[i].urb=NULL; buffers[i].status_flags.val=0; }
    pipe.multi_buffer_control.val=0; pipe.multi_buffer_control.buffer_num_to_fill=NUM_BUFFERS;
    channel.flags.active=false; request.hcd_var=URB_HCD_STATE_PENDING;
    TAILQ_INSERT_TAIL(&pipe.pending_urb_tailq,&request,tailq_entry); pipe.num_urb_pending=1;
    check_flush_lock=true;
}
unsigned rig_checked_flush_callbacks(void) { return checked_flush_callbacks; }
void rig_legacy_messages(void) {
    usb_host_client_event_msg_t event={.event=USB_HOST_CLIENT_EVENT_NEW_DEV};
    for(unsigned n=0;n<17;++n) send_event_msg_to_clients(&event,true,0);
    assert(queue.count==16 && client.dynamic.bounded_num_messages==16);
    assert(risc_usb_host_library_idle(&client)==ESP_ERR_NOT_FINISHED);
    assert(usb_host_client_handle_events(&client,0)==ESP_OK);
    assert(messages==16 && client.dynamic.bounded_num_messages==0 && queue.count==0);
    assert(risc_usb_host_library_idle(&client)==ESP_OK);
}
void rig_packet(unsigned packet) { descriptor.wMaxPacketSize=(uint16_t)packet; }
void rig_direction(bool reading) { descriptor.bEndpointAddress=reading?0x81:1; pipe.ep_char.bEndpointAddress=descriptor.bEndpointAddress; }
void rig_interface(unsigned number,unsigned alternate) { intf_descriptor.bInterfaceNumber=(uint8_t)number; intf_descriptor.bAlternateSetting=(uint8_t)alternate; }
esp_err_t rig_legacy_command(void) { return hcd_pipe_command(&pipe,HCD_PIPE_CMD_HALT); }
esp_err_t rig_legacy_free(void) { return hcd_pipe_free(&pipe); }
