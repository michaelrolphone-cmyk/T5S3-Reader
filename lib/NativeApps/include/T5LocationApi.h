#pragma once
#include <stdint.h>
#include "T5DeviceApi.h"
#include "T5StreamApi.h"
#ifdef __cplusplus
extern "C" {
#endif

#define T5_LOCATION_API_VERSION 1u
typedef uint64_t t5_location_subscription_t;

/* Semantic location.fix.v1 streaming is distinct from passive device
 * observation. Obtain a location.position READ authorization with device API
 * v3 request() (or a trusted firmware-granted v2 acquire()) first. A manifest
 * dependency alone does not authorize this API. No app-provided owner ID,
 * physical grant, ELF callback or driver pointer is accepted. */
typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    /* Resolves the authorized device generation, starts its installable GNSS
     * provider if necessary, and opens one READ-only typed record stream.
     * Each subscription has its own bounded four-record queue. */
    t5_stream_result_t (*subscribe)(t5_device_lease_t authorization,
        t5_location_subscription_t *subscription, t5_stream_t *stream);
    /* Cooperative owner-task poll. OK means the driver was polled or a pending
     * record was retried; it does not imply a new fix. Read the resulting
     * location.fix.v1 records with stream API v2 record_read(). The generic
     * stream scheduler must NEVER execute the GNSS ELF's UART reader. */
    t5_stream_result_t (*poll)(t5_device_lease_t authorization);
    /* Release this invocation's subscription and its shared device claim.
     * The source grant stays owned by the GNSS driver. */
    t5_stream_result_t (*unsubscribe)(t5_location_subscription_t subscription);
} t5_location_api_v1;

const t5_location_api_v1 *t5_location_get_api(uint32_t api_version);
#ifdef __cplusplus
}
#endif
