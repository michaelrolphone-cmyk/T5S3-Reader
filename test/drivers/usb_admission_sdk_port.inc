/* Memory/HAL fault port. All admission, parser, SDK map/list, close/refcount,
 * pool and token code below is the production adaptation. */
#define MALLOC_CAP_INTERNAL 1u
#define MALLOC_CAP_8BIT 2u
#define MALLOC_CAP_DMA 4u
#define PROCESS_PENDING_FLAG_USBH 1u
#define DEV_FLAG_ACTION_FREE_AND_RECOVER 0x40u
#define DEV_FLAG_ACTION_PORT_DISABLE 0x80u
static bool memory_forbidden = true;
static unsigned allocations, frees, live_allocations, fail_allocation;
static void *heap_caps_calloc(size_t count,size_t size,uint32_t caps) {
    assert(!memory_forbidden && (caps&MALLOC_CAP_INTERNAL));
    if(++allocations==fail_allocation) return NULL;
    void *p=calloc(count,size); if(p) ++live_allocations; return p;
}
static void *heap_caps_aligned_calloc(size_t align,size_t count,size_t size,uint32_t caps) {
    assert(!memory_forbidden && align==512 && (caps&MALLOC_CAP_DMA) && (caps&MALLOC_CAP_INTERNAL));
    if(++allocations==fail_allocation) return NULL;
    void *p=NULL;
    if(posix_memalign(&p,align,count*size)) return NULL;
    memset(p,0,count*size); ++live_allocations; return p;
}
static void heap_caps_free(void *p) {
    assert(!memory_forbidden);
    if(p) { assert(live_allocations); --live_allocations; ++frees; free(p); }
}
typedef struct { usb_dwc_hal_chan_t *channels[8]; unsigned occupied,allocated; } fake_hal_t;
static bool usb_dwc_hal_chan_alloc(void *context,usb_dwc_hal_chan_t *channel,void *pipe) {
    (void)pipe; assert(hcd_lock.depth>0);
    fake_hal_t *hal=context;
    for(unsigned i=0;i<8;++i) if(!(hal->occupied&(1u<<i))) {
        memset(channel,0,sizeof(*channel)); channel->flags.chan_idx=i;
        hal->channels[i]=channel; hal->occupied|=1u<<i; ++hal->allocated; return true;
    }
    return false;
}
static void usb_dwc_hal_chan_free(void *context,usb_dwc_hal_chan_t *channel) {
    assert(hcd_lock.depth>0 && !channel->flags.active);
    fake_hal_t *hal=context;
    unsigned i=channel->flags.chan_idx;
    assert(i<8 && hal->channels[i]==channel && (hal->occupied&(1u<<i)));
    hal->channels[i]=NULL; hal->occupied&=~(1u<<i); --hal->allocated;
}
static void usb_dwc_hal_chan_set_ep_char(void *context,usb_dwc_hal_chan_t *channel,usb_dwc_hal_ep_char_t *ep) {
    (void)context; assert(hcd_lock.depth>0 && !channel->flags.active);
    assert(ep->mps>0 && ep->mps<=512);
}
static struct { struct {
    TAILQ_HEAD(dev_idle,device_s) devs_idle_tailq;
    TAILQ_HEAD(dev_pending,device_s) devs_pending_tailq;
} dynamic;
#ifdef RISC_USB_CONTROL_TEST
struct { bool (*notif_cb)(int,bool,void *); void *notif_cb_arg; } constant;
#endif
} test_usbh;
static __typeof__(test_usbh) *p_usbh_obj=&test_usbh;
static void endpoint_free(usb_device_handle_t device,endpoint_t *ep) { (void)device;(void)ep;abort(); }
static void interface_free(interface_t *intf) { (void)intf;abort(); }
