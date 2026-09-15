#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define T5_USB_API_VERSION 1u
#define T5_USB_PRODUCT_MAX 64u

typedef enum {
    T5_USB_STATUS_UNSUPPORTED = 0,
    T5_USB_STATUS_OFF = 1,
    T5_USB_STATUS_WAITING = 2,
    T5_USB_STATUS_CONFIGURING = 3,
    T5_USB_STATUS_READY = 4,
    T5_USB_STATUS_ERROR = 5,
} t5_usb_status_t;

typedef enum {
    T5_USB_PARITY_NONE = 0,
    T5_USB_PARITY_ODD = 1,
    T5_USB_PARITY_EVEN = 2,
    T5_USB_PARITY_MARK = 3,
    T5_USB_PARITY_SPACE = 4,
} t5_usb_parity_t;

typedef struct {
    uint32_t baud_rate;
    uint8_t data_bits;
    uint8_t parity;
    uint8_t stop_bits; /* 1 or 2 */
    uint8_t reserved;
} t5_usb_line_coding_t;

typedef struct {
    uint8_t status;
    uint8_t connected;
    uint8_t dtr;
    uint8_t rts;
    uint16_t vid;
    uint16_t pid;
    int32_t last_error;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t dropped_rx_bytes;
    t5_usb_line_coding_t line_coding;
    char product[T5_USB_PRODUCT_MAX];
} t5_usb_serial_state_t;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;

    /* USB OTG/host hardware and serial-device drivers remain entirely firmware-owned. */
    bool (*supported)(void);
    bool (*serial_start)(const t5_usb_line_coding_t *coding);
    void (*serial_stop)(void);
    bool (*serial_read_state)(t5_usb_serial_state_t *state);
    bool (*serial_set_line_coding)(const t5_usb_line_coding_t *coding);
    bool (*serial_set_control_lines)(bool dtr, bool rts);
    size_t (*serial_read)(uint8_t *data, size_t capacity);
    size_t (*serial_write)(const uint8_t *data, size_t length);
} t5_usb_api_v1;

const t5_usb_api_v1 *t5_usb_get_api(uint32_t api_version);

#ifdef __cplusplus
}
#endif