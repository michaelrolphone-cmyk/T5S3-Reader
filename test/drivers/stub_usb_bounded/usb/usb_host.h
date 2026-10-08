#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;
enum { ESP_OK=0, ESP_ERR_INVALID_ARG=0x102, ESP_ERR_INVALID_STATE=0x103,
       ESP_ERR_NOT_FOUND=0x105, ESP_ERR_NOT_SUPPORTED=0x106,
       ESP_ERR_TIMEOUT=0x107, ESP_ERR_NOT_FINISHED=0x10c };
typedef void *usb_device_handle_t;
typedef void *usb_host_client_handle_t;
typedef enum { USB_TRANSFER_STATUS_COMPLETED, USB_TRANSFER_STATUS_ERROR,
    USB_TRANSFER_STATUS_TIMED_OUT, USB_TRANSFER_STATUS_CANCELED,
    USB_TRANSFER_STATUS_STALL, USB_TRANSFER_STATUS_OVERFLOW,
    USB_TRANSFER_STATUS_SKIPPED, USB_TRANSFER_STATUS_NO_DEVICE } usb_transfer_status_t;
typedef struct { int num_bytes, actual_num_bytes; usb_transfer_status_t status; } usb_isoc_packet_desc_t;
struct usb_transfer_s;
typedef void (*usb_transfer_cb_t)(struct usb_transfer_s *);
typedef struct usb_transfer_s {
    uint8_t *data_buffer;
    size_t data_buffer_size;
    int num_bytes, actual_num_bytes;
    uint32_t flags;
    usb_device_handle_t device_handle;
    uint8_t bEndpointAddress;
    usb_transfer_status_t status;
    uint32_t timeout;
    usb_transfer_cb_t callback;
    void *context;
    int num_isoc_packets;
    usb_isoc_packet_desc_t isoc_packet_desc[0];
} usb_transfer_t;
enum { USB_HOST_CLIENT_EVENT_NEW_DEV, USB_HOST_CLIENT_EVENT_DEV_GONE };
typedef struct { int event; union { struct { uint8_t address; } new_dev;
    struct { usb_device_handle_t dev_hdl; } dev_gone; }; } usb_host_client_event_msg_t;
typedef void (*usb_host_client_event_cb_t)(const usb_host_client_event_msg_t *, void *);
