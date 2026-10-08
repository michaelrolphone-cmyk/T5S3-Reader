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
#define EP_NUM_MIN 1
#define EP_NUM_MAX 16
#define USB_B_ENDPOINT_ADDRESS_EP_NUM_MASK 0x0f
#define USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK 0x80
#define USB_EP_DESC_GET_MPS(ep) ((ep)->wMaxPacketSize)
#define USB_EP_DESC_GET_XFERTYPE(ep) ((ep)->bmAttributes & 3)
enum { USB_TRANSFER_TYPE_CTRL, USB_TRANSFER_TYPE_ISOCHRONOUS,
       USB_TRANSFER_TYPE_BULK, USB_TRANSFER_TYPE_INTR };
enum { USB_PRIV_XFER_TYPE_CTRL, USB_PRIV_XFER_TYPE_ISOCHRONOUS,
       USB_PRIV_XFER_TYPE_BULK, USB_PRIV_XFER_TYPE_INTR };
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
typedef struct { struct { bool active, halt_requested; } flags;
    usb_dwc_hal_chan_event_t event; usb_dwc_hal_chan_error_t error; } usb_dwc_hal_chan_t;
typedef struct { int type, mps; uint8_t bEndpointAddress; } usb_dwc_hal_ep_char_t;
typedef void *hcd_pipe_handle_t;
typedef void *usb_phy_handle_t;
typedef void *TaskHandle_t;
typedef bool (*hcd_pipe_callback_t)(hcd_pipe_handle_t,hcd_pipe_event_t,void *,bool);
typedef struct { uint8_t bEndpointAddress, bmAttributes; uint16_t wMaxPacketSize; } usb_ep_desc_t;
typedef struct { uint8_t bInterfaceNumber, bAlternateSetting, bNumEndpoints; } usb_intf_desc_t;
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
static void usb_dwc_hal_chan_free(void *hal, usb_dwc_hal_chan_t *channel) { (void)hal; (void)channel; abort(); }
static void usb_dwc_hal_xfer_desc_parse(void *list,int index,int *left,int *status) {
    assert(index==0); *left=((usb_dwc_ll_dma_qtd_t *)list)->remainder; *status=0;
}
