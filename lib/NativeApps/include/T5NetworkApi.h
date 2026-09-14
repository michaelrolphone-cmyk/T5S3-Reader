#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define T5_NETWORK_API_VERSION 1u
#define T5_HTTP_METHOD_GET 0u
#define T5_HTTP_METHOD_POST 1u
#define T5_HTTP_RESPONSE_TRUNCATED (1u << 0)

typedef struct {
    const char *name;
    const char *value;
} t5_http_header_t;

typedef struct {
    int32_t transport_error;
    int32_t status_code;
    size_t response_bytes;
    uint8_t flags;
} t5_http_result_t;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;

    // Reports the firmware's current station connectivity without exposing the
    // Arduino WiFi object or ESP-IDF networking internals to native apps.
    bool (*wifi_connected)(void);

    // Generic HTTPS/HTTP request primitive. Native apps own service-specific
    // URLs, headers, request bodies, certificates and response parsing. The
    // response buffer is always NUL-terminated when response_capacity > 0.
    // Returns true when the transport completed, even for non-2xx HTTP status
    // codes. `transport_error` contains the esp_err_t value on failure.
    bool (*http_request)(const char *url,
                         uint8_t method,
                         const t5_http_header_t *headers,
                         uint32_t header_count,
                         const void *body,
                         size_t body_size,
                         const char *cert_pem,
                         uint32_t timeout_ms,
                         char *response,
                         size_t response_capacity,
                         t5_http_result_t *result);
} t5_network_api_v1;

const t5_network_api_v1 *t5_network_get_api(uint32_t api_version);

#ifdef __cplusplus
}
#endif
