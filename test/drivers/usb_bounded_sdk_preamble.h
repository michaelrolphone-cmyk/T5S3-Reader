/* Simulated OS/HAL boundary for exact staged ESP-IDF C bodies. No hardware. */
#include "usb_bounded_rig.h"
#include "BoundedIdf.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#define __containerof(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define NUM_BUFFERS 2
#define XFER_LIST_LEN_BULK 2
#define XFER_LIST_LEN_CTRL 3
#define XFER_LIST_LEN_INTR 32
#define XFER_LIST_LEN_ISOC 32
#define FRAME_LIST_LEN 32
#define CTRL_EP_MAX_MPS_FS 64
#define CTRL_EP_MAX_MPS_LS 8
#define USB_DWC_HAL_NUM_CHAN 8
#define USB_DWC_HAL_DMA_MEM_ALIGN 512
#define EP_NUM_MIN 1
#define EP_NUM_MAX 16
#define USB_B_ENDPOINT_ADDRESS_EP_NUM_MASK 0x0f
#define USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK 0x80
#define USB_EP_DESC_GET_MPS(ep) ((ep)->wMaxPacketSize)
#define USB_EP_DESC_GET_XFERTYPE(ep) ((ep)->bmAttributes & 3)
#define USB_EP_DESC_GET_EP_DIR(ep) ((ep)->bEndpointAddress & 0x80u)
typedef enum { USB_TRANSFER_TYPE_CTRL, USB_TRANSFER_TYPE_ISOCHRONOUS,
       USB_TRANSFER_TYPE_BULK, USB_TRANSFER_TYPE_INTR } usb_transfer_type_t;
typedef enum { USB_PRIV_XFER_TYPE_CTRL, USB_PRIV_XFER_TYPE_ISOCHRONOUS,
       USB_PRIV_XFER_TYPE_BULK, USB_PRIV_XFER_TYPE_INTR } usb_priv_xfer_type_t;
typedef enum { USB_SPEED_LOW, USB_SPEED_FULL } usb_speed_t;
typedef enum { USB_DEVICE_STATE_CONFIGURED, USB_DEVICE_STATE_NOT_ATTACHED } usb_device_state_t;
typedef enum { HCD_PIPE_EVENT_NONE, HCD_PIPE_EVENT_URB_DONE,
    HCD_PIPE_EVENT_ERROR_XFER, HCD_PIPE_EVENT_ERROR_URB_NOT_AVAIL,
    HCD_PIPE_EVENT_ERROR_OVERFLOW, HCD_PIPE_EVENT_ERROR_STALL } hcd_pipe_event_t;
typedef enum { HCD_PIPE_STATE_ACTIVE, HCD_PIPE_STATE_HALTED } hcd_pipe_state_t;
typedef enum { HCD_PIPE_CMD_HALT, HCD_PIPE_CMD_FLUSH, HCD_PIPE_CMD_CLEAR } hcd_pipe_cmd_t;
enum { HCD_PORT_STATE_ENABLED };
enum { URB_HCD_STATE_IDLE, URB_HCD_STATE_PENDING, URB_HCD_STATE_INFLIGHT, URB_HCD_STATE_DONE };
typedef enum { USB_DWC_HAL_CHAN_EVENT_NONE, USB_DWC_HAL_CHAN_EVENT_CPLT,
    USB_DWC_HAL_CHAN_EVENT_ERROR, USB_DWC_HAL_CHAN_EVENT_HALT_REQ } usb_dwc_hal_chan_event_t;
typedef enum { USB_DWC_HAL_CHAN_ERROR_XCS_XACT, USB_DWC_HAL_CHAN_ERROR_BNA,
    USB_DWC_HAL_CHAN_ERROR_PKT_BBL, USB_DWC_HAL_CHAN_ERROR_STALL } usb_dwc_hal_chan_error_t;
#define USB_DWC_HAL_XFER_DESC_STS_SUCCESS 0
typedef struct { int remainder; } usb_dwc_ll_dma_qtd_t;
typedef struct { struct { bool active, halt_requested; unsigned chan_idx; } flags;
    usb_dwc_hal_chan_event_t event; usb_dwc_hal_chan_error_t error; } usb_dwc_hal_chan_t;
typedef struct { int type, mps; uint8_t bEndpointAddress, dev_addr; bool ls_via_fs_hub;
    struct { int interval, phase_offset_frames; } periodic; } usb_dwc_hal_ep_char_t;
typedef void *hcd_pipe_handle_t;
typedef void *hcd_port_handle_t;
typedef void *usb_phy_handle_t;
typedef void *TaskHandle_t;
typedef bool (*hcd_pipe_callback_t)(hcd_pipe_handle_t,hcd_pipe_event_t,void *,bool);
typedef struct __attribute__((packed)) { uint8_t bLength,bDescriptorType,bEndpointAddress,bmAttributes;
    uint16_t wMaxPacketSize; uint8_t bInterval; } usb_ep_desc_t;
typedef struct __attribute__((packed)) { uint8_t bLength,bDescriptorType,bInterfaceNumber,bAlternateSetting,
    bNumEndpoints,bInterfaceClass,bInterfaceSubClass,bInterfaceProtocol,iInterface; } usb_intf_desc_t;
typedef struct __attribute__((packed)) { uint8_t bLength,bDescriptorType; uint16_t wTotalLength;
    uint8_t bNumInterfaces,bConfigurationValue,iConfiguration,bmAttributes,bMaxPower; } usb_config_desc_t;
typedef struct { uint16_t idVendor,idProduct; } usb_device_desc_t;
typedef struct { uint8_t value; } usb_str_desc_t;
#ifdef RISC_USB_CONTROL_TEST
typedef struct __attribute__((packed)) { uint8_t bmRequestType,bRequest; uint16_t wValue,wIndex,wLength; } usb_setup_packet_t;
#define USB_BM_REQUEST_TYPE_DIR_IN 0x80
#define USB_DWC_HAL_XFER_DESC_FLAG_SETUP 1
#define USB_DWC_HAL_XFER_DESC_FLAG_HOC 2
#define USB_DWC_HAL_XFER_DESC_FLAG_IN 4
#endif
typedef struct { int in_mps, non_periodic_out_mps, periodic_out_mps; } fifo_mps_limits_t;
typedef struct { hcd_pipe_callback_t callback; void *callback_arg,*context; const usb_ep_desc_t *ep_desc;
    usb_speed_t dev_speed; uint8_t dev_addr; } hcd_pipe_config_t;
typedef struct { const usb_ep_desc_t *ep_desc; hcd_pipe_callback_t pipe_cb; void *pipe_cb_arg,*context; } usbh_ep_config_t;
typedef struct { int depth; bool blocked; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED {0, false}
typedef portMUX_TYPE *SemaphoreHandle_t;
typedef struct { unsigned count; usb_host_client_event_msg_t item; } fake_queue_t;
typedef fake_queue_t *QueueHandle_t;
static portMUX_TYPE host_lock, hcd_lock, usbh_lock;
static unsigned halt_requests, messages;
static bool replenish_events;
static bool check_flush_lock;
static unsigned checked_flush_callbacks;
static int xPortEnterCriticalTimeout(portMUX_TYPE *lock, unsigned ticks) {
    assert(ticks == 0);
    if (lock->blocked && !lock->depth) return 0;
    ++lock->depth; return 1;
}
static void sim_enter(portMUX_TYPE *lock) { assert(!lock->blocked || lock->depth); ++lock->depth; }
static void sim_exit(portMUX_TYPE *lock) { assert(lock->depth > 0); --lock->depth; }
#define HOST_ENTER_CRITICAL() sim_enter(&host_lock)
#define HOST_EXIT_CRITICAL() sim_exit(&host_lock)
#define HOST_ENTER_CRITICAL_SAFE() HOST_ENTER_CRITICAL()
#define HOST_EXIT_CRITICAL_SAFE() HOST_EXIT_CRITICAL()
#define HCD_ENTER_CRITICAL() sim_enter(&hcd_lock)
#define HCD_EXIT_CRITICAL() sim_exit(&hcd_lock)
#ifdef RISC_USB_CONTROL_TEST
static void (*dispatch_hook)(unsigned);
#define HCD_EXIT_CRITICAL_ISR() do { HCD_EXIT_CRITICAL(); if(dispatch_hook) dispatch_hook(0); } while(0)
#define HCD_ENTER_CRITICAL_ISR() do { if(dispatch_hook) dispatch_hook(1); HCD_ENTER_CRITICAL(); } while(0)
#else
#define HCD_EXIT_CRITICAL_ISR() HCD_EXIT_CRITICAL()
#define HCD_ENTER_CRITICAL_ISR() HCD_ENTER_CRITICAL()
#endif
#define USBH_EXIT_CRITICAL() sim_exit(&usbh_lock)
#define HCD_CHECK(c,r) do { if (!(c)) return (r); } while(0)
#define HCD_CHECK_FROM_CRIT(c,r) do { if (!(c)) { HCD_EXIT_CRITICAL(); return (r); } } while(0)
/* No FreeRTOS semaphore/queue operation is available to the bounded source.
 * Any accidental call is a compiler/link error rather than a fake success. */
static bool usb_dwc_hal_chan_request_halt(usb_dwc_hal_chan_t *channel) {
    ++halt_requests;
    if(channel->flags.active) { channel->flags.halt_requested=true; return false; }
    return true;
}
static void usb_dwc_hal_chan_mark_halted(usb_dwc_hal_chan_t *channel) { channel->flags.active=false; }
static usb_dwc_hal_chan_event_t usb_dwc_hal_chan_decode_intr(usb_dwc_hal_chan_t *channel) {
    channel->flags.active=false;
    usb_dwc_hal_chan_event_t event=channel->event;
    if(event!=USB_DWC_HAL_CHAN_EVENT_ERROR && channel->flags.halt_requested) {
        channel->flags.halt_requested=false; return USB_DWC_HAL_CHAN_EVENT_HALT_REQ;
    }
    return event;
}
static usb_dwc_hal_chan_error_t usb_dwc_hal_chan_get_error(usb_dwc_hal_chan_t *channel) { return channel->error; }
static int usb_dwc_hal_chan_get_qtd_idx(usb_dwc_hal_chan_t *channel) { (void)channel; return 0; }
#ifndef RISC_USB_ADMISSION_TEST
static void usb_dwc_hal_chan_free(void *hal, usb_dwc_hal_chan_t *channel) { (void)hal; (void)channel; abort(); }
#endif
static void usb_dwc_hal_xfer_desc_parse(void *list,int index,int *left,int *status) {
    assert(index>=0 && index<3); *left=((usb_dwc_ll_dma_qtd_t *)list)[index].remainder; *status=0;
}
#ifdef RISC_USB_CONTROL_TEST
static bool control_hal_in;
static unsigned control_hal_pid, control_hal_stage;
static void usb_dwc_hal_xfer_desc_fill(void *list,int index,void *data,int size,unsigned flags) {
    assert(index>=0 && index<3 && size>=0); (void)data; (void)flags;
    ((usb_dwc_ll_dma_qtd_t *)list)[index].remainder=size;
}
static void usb_dwc_hal_xfer_desc_clear(void *list,int index) { ((usb_dwc_ll_dma_qtd_t *)list)[index].remainder=0; }
static void usb_dwc_hal_chan_set_dir(usb_dwc_hal_chan_t *chan,bool in) { assert(!chan->flags.active); control_hal_in=in; }
static void usb_dwc_hal_chan_set_pid(usb_dwc_hal_chan_t *chan,int pid) { assert(!chan->flags.active); control_hal_pid=pid; }
static void usb_dwc_hal_chan_activate(usb_dwc_hal_chan_t *chan,void *list,int count,int start) {
    (void)list; assert(!chan->flags.active && count==3 && start>=0 && start<3);
    control_hal_stage=start; chan->flags.active=true;
}
#endif
