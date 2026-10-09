#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;
enum { ESP_OK=0, ESP_ERR_NO_MEM=0x101, ESP_ERR_INVALID_ARG=0x102, ESP_ERR_INVALID_STATE=0x103,
       ESP_ERR_INVALID_SIZE=0x104,
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
struct usb_config_desc_t {uint8_t bLength,bDescriptorType;uint16_t wTotalLength;};
struct usb_device_desc_t {uint16_t idVendor,idProduct;};
struct usb_host_config_t {bool skip_phy_setup;int intr_flags;};
struct usb_host_client_config_t {bool is_synchronous;int max_num_event_msg;struct{usb_host_client_event_cb_t client_event_callback;void*callback_arg;}async;};
enum {USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS=1,USB_HOST_LIB_EVENT_FLAGS_ALL_FREE=2};
esp_err_t usb_host_install(const usb_host_config_t*);
esp_err_t usb_host_uninstall();
esp_err_t usb_host_client_register(const usb_host_client_config_t*,usb_host_client_handle_t*);
esp_err_t usb_host_client_deregister(usb_host_client_handle_t);
esp_err_t usb_host_lib_handle_events(unsigned,uint32_t*);
esp_err_t usb_host_client_handle_events(usb_host_client_handle_t,unsigned);
esp_err_t usb_host_device_open(usb_host_client_handle_t,uint8_t,usb_device_handle_t*);
esp_err_t usb_host_device_close(usb_host_client_handle_t,usb_device_handle_t);
esp_err_t usb_host_device_free_all();
esp_err_t usb_host_get_device_descriptor(usb_device_handle_t,const usb_device_desc_t**);
esp_err_t usb_host_get_active_config_descriptor(usb_device_handle_t,const usb_config_desc_t**);
esp_err_t usb_host_interface_claim(usb_host_client_handle_t,usb_device_handle_t,uint8_t,uint8_t);
esp_err_t usb_host_interface_release(usb_host_client_handle_t,usb_device_handle_t,uint8_t);
esp_err_t usb_host_transfer_alloc(size_t,int,usb_transfer_t**);
esp_err_t usb_host_transfer_free(usb_transfer_t*);
esp_err_t usb_host_transfer_submit(usb_transfer_t*);
esp_err_t usb_host_transfer_submit_control(usb_host_client_handle_t,usb_transfer_t*);
esp_err_t usb_host_endpoint_halt(usb_device_handle_t,uint8_t);
esp_err_t usb_host_endpoint_flush(usb_device_handle_t,uint8_t);
esp_err_t usb_host_endpoint_clear(usb_device_handle_t,uint8_t);
