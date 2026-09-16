#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RiscRTE semantic device observation, ABI v1. Observation does not authorize
 * hardware access. Device handles identify a generation, not a USB VID/PID.
 * All calls must originate on the active application's execution task.
 */
#define T5_DEVICE_API_VERSION 1u
#define T5_DEVICE_IDENTITY_MAX 48u
#define T5_DEVICE_LABEL_MAX 48u
#define T5_DEVICE_PROVIDER_MAX 32u
#define T5_DEVICE_CAPABILITY_MAX 40u
#define T5_DEVICE_CAPABILITY_COUNT 6u

typedef uint32_t t5_device_handle_t;
typedef uint32_t t5_device_subscription_t;
typedef int32_t t5_device_result_t;

enum {
    T5_DEVICE_OK = 0,
    T5_DEVICE_NEXT = 1,
    T5_DEVICE_EMPTY = 2,
    T5_DEVICE_GAP = 3,
    T5_DEVICE_INVALID = -1,
    T5_DEVICE_DENIED = -2,
    T5_DEVICE_STALE = -3,
    T5_DEVICE_LIMIT = -4,
};

typedef enum {
    T5_DEVICE_TRANSPORT_INTERNAL = 0,
    T5_DEVICE_TRANSPORT_UART = 1,
    T5_DEVICE_TRANSPORT_USB = 2,
    T5_DEVICE_TRANSPORT_BLE = 3,
    T5_DEVICE_TRANSPORT_I2C = 4,
    T5_DEVICE_TRANSPORT_SPI = 5,
    T5_DEVICE_TRANSPORT_GPIO = 6,
    T5_DEVICE_TRANSPORT_LORA = 7,
    T5_DEVICE_TRANSPORT_IP = 8,
} t5_device_transport_t;

typedef enum {
    T5_DEVICE_DISCOVERED = 0,
    T5_DEVICE_IDENTIFIED = 1,
    T5_DEVICE_BOUND = 2,
    T5_DEVICE_AVAILABLE = 3,
    T5_DEVICE_BUSY = 4,
    T5_DEVICE_SUSPENDED = 5,
    T5_DEVICE_UNAVAILABLE = 6,
    T5_DEVICE_REMOVED = 7,
    T5_DEVICE_FAILED = 8,
} t5_device_state_t;

typedef enum {
    T5_DEVICE_ADDED = 0,
    T5_DEVICE_STATE_CHANGED = 1,
    T5_DEVICE_CAPABILITY_LOST = 2,
    T5_DEVICE_REMOVAL = 3,
} t5_device_event_kind_t;

typedef struct {
    t5_device_handle_t handle;
    uint8_t state;
    uint8_t transport;
    uint8_t priority;
    uint8_t capability_count;
    char identity[T5_DEVICE_IDENTITY_MAX];
    char label[T5_DEVICE_LABEL_MAX];
    char provider[T5_DEVICE_PROVIDER_MAX];
    char capabilities[T5_DEVICE_CAPABILITY_COUNT][T5_DEVICE_CAPABILITY_MAX];
} t5_device_info_t;

typedef struct {
    uint64_t sequence;
    t5_device_handle_t device;
    uint8_t kind;
    uint8_t previous;
    uint8_t current;
    uint32_t revoked_leases;
    char identity[T5_DEVICE_IDENTITY_MAX];
} t5_device_event_t;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    /* All-or-nothing snapshot. *count receives required capacity on LIMIT;
     * capacity=0 and out=NULL queries the required capacity. */
    t5_device_result_t (*inventory)(t5_device_info_t *out, uint32_t capacity,
                                     uint32_t *count);
    /* Starts at the current sequence; does not replay previous invocations. */
    t5_device_result_t (*subscribe)(t5_device_subscription_t *out);
    /* Atomic resnapshot and cursor acknowledgement. Mandatory after GAP;
     * insufficient capacity does NOT acknowledge the loss. */
    t5_device_result_t (*snapshot)(t5_device_subscription_t subscription,
                                    t5_device_info_t *out, uint32_t capacity,
                                    uint32_t *count);
    /* NEXT writes an event; EMPTY means none; GAP requires snapshot before
     * another event can be read. missed (optional) counts overwritten events. */
    t5_device_result_t (*poll)(t5_device_subscription_t subscription,
                                t5_device_event_t *out, uint64_t *missed);
    t5_device_result_t (*unsubscribe)(t5_device_subscription_t subscription);
} t5_device_api_v1;

const t5_device_api_v1 *t5_device_get_api(uint32_t version);

#ifdef __cplusplus
}
#endif
