/* Keep the normal Serial Monitor interface intact while surfacing capability
 * errors in its scrollable terminal. The diagnostic is captured immediately
 * after acquire(), before a retry can overwrite it, and requires no USB log
 * connection. */
#include "T5SerialPortApi.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const t5_serial_port_api_v1* providerSerialApi;
static t5_serial_port_api_v1 displaySerialApi;
static t5_serial_result_t displayAcquire(const t5_serial_port_request_t* request,
                                        t5_serial_port_lease_t* lease,
                                        t5_stream_t* rx, t5_stream_t* tx);
static const t5_serial_port_api_v1* displaySerialGetApi(uint32_t version);

#define t5_serial_port_get_api displaySerialGetApi
#include "serial_monitor_implementation.inc"
#undef t5_serial_port_get_api

static t5_serial_result_t displayAcquire(const t5_serial_port_request_t* request,
                                        t5_serial_port_lease_t* lease,
                                        t5_stream_t* rx, t5_stream_t* tx) {
    static char previousFailure[384];
    char failure[384];
    const t5_serial_result_t result = providerSerialApi->acquire(request, lease, rx, tx);
    if (result == T5_SERIAL_OK && lease && *lease && rx && *rx && tx && *tx) {
        previousFailure[0] = 0;
        return result;
    }
    t5_serial_diagnostic_t details = {0};
    const size_t required = offsetof(t5_serial_port_api_v1, last_diagnostic) +
                            sizeof(providerSerialApi->last_diagnostic);
    if (providerSerialApi->struct_size >= required &&
        providerSerialApi->last_diagnostic &&
        providerSerialApi->last_diagnostic(&details) && details.detail[0]) {
        details.detail[sizeof(details.detail) - 1u] = 0;
        snprintf(failure, sizeof(failure), "%s", details.detail);
    } else {
        snprintf(failure, sizeof(failure),
                 "serial.port acquire rc=%ld; detailed provider diagnostics unavailable",
                 (long)result);
    }
    if (strcmp(previousFailure, failure) != 0) {
        append_notice(failure);
        snprintf(previousFailure, sizeof(previousFailure), "%s", failure);
    }
    scroll_from_bottom = 0;
    return result;
}

static const t5_serial_port_api_v1* displaySerialGetApi(uint32_t version) {
    providerSerialApi = t5_serial_port_get_api(version);
    if (!providerSerialApi) return NULL;
    displaySerialApi = *providerSerialApi;
    displaySerialApi.acquire = displayAcquire;
    return &displaySerialApi;
}
