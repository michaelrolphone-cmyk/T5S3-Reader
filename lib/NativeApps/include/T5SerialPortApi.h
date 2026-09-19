#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "T5StreamApi.h"
#ifdef __cplusplus
extern "C" {
#endif

#define T5_SERIAL_PORT_API_VERSION 1u
#define T5_SERIAL_PORT_CAPABILITY "serial.port"
#define T5_SERIAL_PORT_LABEL_MAX 96u
#define T5_SERIAL_DIAGNOSTIC_MAX 256u

typedef uint32_t t5_serial_port_lease_t;
typedef uint32_t t5_serial_device_t;
typedef int32_t t5_serial_result_t;

enum {
    T5_SERIAL_OK = 0,
    T5_SERIAL_INVALID = -1,
    T5_SERIAL_DENIED = -2,
    T5_SERIAL_LIMIT = -3,
    T5_SERIAL_UNSUPPORTED = -4,
    T5_SERIAL_IO = -5,
    T5_SERIAL_DISCONNECTED = -6,
    T5_SERIAL_BUSY = -8,
    T5_SERIAL_CLOSED = -9,
};

typedef enum {
    T5_SERIAL_STATUS_UNAVAILABLE = 0,
    T5_SERIAL_STATUS_OFF = 1,
    T5_SERIAL_STATUS_WAITING = 2,
    T5_SERIAL_STATUS_CONFIGURING = 3,
    T5_SERIAL_STATUS_READY = 4,
    T5_SERIAL_STATUS_ERROR = 5,
} t5_serial_status_t;

typedef enum {
    T5_SERIAL_PARITY_NONE = 0,
    T5_SERIAL_PARITY_ODD = 1,
    T5_SERIAL_PARITY_EVEN = 2,
    T5_SERIAL_PARITY_MARK = 3,
    T5_SERIAL_PARITY_SPACE = 4,
} t5_serial_parity_t;

typedef enum {
    T5_SERIAL_FLOW_NONE = 0,
    T5_SERIAL_FLOW_RTS_CTS = 1,
} t5_serial_flow_control_t;

typedef struct {
    uint32_t baud_rate;
    uint8_t data_bits;
    uint8_t parity;
    uint8_t stop_bits;
    uint8_t flow_control;
} t5_serial_config_t;

typedef struct {
    /* Zero selects the default usable provider/device. Non-zero is reserved for
     * a device handle previously returned by this capability. */
    t5_serial_device_t device;
    t5_serial_config_t config;
} t5_serial_port_request_t;

typedef struct {
    uint8_t status;
    uint8_t connected;
    uint8_t dtr;
    uint8_t rts;
    int32_t last_error;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t dropped_rx_bytes;
    t5_serial_device_t device;
    t5_serial_config_t config;
    /* Provider-owned presentation string. Consumers may display but must not
     * parse it for transport-specific identity. */
    char device_label[T5_SERIAL_PORT_LABEL_MAX];
} t5_serial_port_state_t;

/* Optional post-acquisition diagnostic. It remains available when no lease
 * exists, unlike read_status(). The generic result and provider error are
 * distinct; detail is a bounded, human-readable diagnostic, not a command or
 * a transport-specific API. It must not include received serial payloads. */
typedef struct {
    t5_serial_result_t result;
    int32_t provider_error;
    char detail[T5_SERIAL_DIAGNOSTIC_MAX];
} t5_serial_diagnostic_t;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    const char *capability_id;

    /* The lease owns both stream handles. release() closes them. */
    t5_serial_result_t (*acquire)(const t5_serial_port_request_t *request,
                                  t5_serial_port_lease_t *lease,
                                  t5_stream_t *rx_stream,
                                  t5_stream_t *tx_stream);
    t5_serial_result_t (*configure)(t5_serial_port_lease_t lease,
                                    const t5_serial_config_t *config);
    t5_serial_result_t (*read_status)(t5_serial_port_lease_t lease,
                                      t5_serial_port_state_t *state);
    t5_serial_result_t (*set_control_lines)(t5_serial_port_lease_t lease,
                                            bool dtr, bool rts);
    t5_serial_result_t (*release)(t5_serial_port_lease_t lease);

    /* ABI-v1 append-only extension. Check struct_size and pointer before use;
     * older providers remain valid and report only the acquire return code.
     * C++ default avoids -Wmissing-field-initializers for legacy tables. */
#ifdef __cplusplus
    bool (*last_diagnostic)(t5_serial_diagnostic_t *out) = nullptr;
#else
    bool (*last_diagnostic)(t5_serial_diagnostic_t *out);
#endif
} t5_serial_port_api_v1;

const t5_serial_port_api_v1 *t5_serial_port_get_api(uint32_t version);

#ifdef __cplusplus
}
#endif
