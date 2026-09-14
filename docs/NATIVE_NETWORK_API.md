# Native network service

`T5NetworkApi` gives SD-installed native ELF applications a small firmware-owned transport layer without embedding application-specific network services in the firmware.

The public header is `lib/NativeApps/include/T5NetworkApi.h`. Request version 1 with:

```c
#include "T5NetworkApi.h"

const t5_network_api_v1 *network = t5_network_get_api(T5_NETWORK_API_VERSION);
if (!network) return;
```

Callers should verify `struct_size` through the last member they require.

## Responsibilities

The firmware owns only the platform transport mechanics:

- reporting whether station Wi-Fi is currently connected;
- creating and performing an ESP HTTP client request;
- applying caller-supplied headers;
- temporarily disabling Wi-Fi power saving during the request and restoring the prior mode afterward;
- watchdog servicing while the request is active;
- returning the transport error, HTTP status, response length and response-truncated flag.

The native application owns all service policy and protocol details:

- endpoint and URL;
- HTTP method;
- service-specific headers and authentication;
- request body and serialization;
- TLS certificate bundle supplied to the request;
- HTTP status policy;
- response parsing;
- retries and application-level error handling.

This boundary is deliberate. For example, `Apps/llm_ask.c` owns the complete LLM7 integration, including its endpoint, model, prompt, TLS roots and JSON parsing. Changing the LLM provider therefore requires an app update rather than a firmware update.

## Connectivity

```c
if (!network->wifi_connected()) {
    // Ask the firmware system-UI service to open its standard Wi-Fi picker.
}
```

Native apps that need an interactive Wi-Fi picker should use the append-only `wifi_request` / `wifi_take_result` members of `T5SystemUiApi`. The firmware unloads the ELF, runs its standard `WifiSelectionActivity`, then relaunches the same ELF with the result available for consumption. This is the same lifecycle used by the native keyboard handoff.

## HTTP request

```c
static const t5_http_header_t headers[] = {
    {"Content-Type", "application/json"},
    {"Accept", "application/json"},
};

t5_http_result_t result = {0};
char response[4096];

bool completed = network->http_request(
    "https://example.com/v1/request",
    T5_HTTP_METHOD_POST,
    headers,
    sizeof(headers) / sizeof(headers[0]),
    request_json,
    strlen(request_json),
    service_ca_bundle,
    30000,
    response,
    sizeof(response),
    &result);
```

`http_request()` returns `true` when the transport completed. A completed request can still have a non-2xx HTTP status, so applications must inspect `result.status_code` themselves. On transport failure, `result.transport_error` contains the underlying `esp_err_t` value.

When the received body is larger than the caller's response buffer, the buffer remains NUL-terminated, `result.response_bytes` reports the full number of bytes observed by the transport, and `T5_HTTP_RESPONSE_TRUNCATED` is set in `result.flags`.

## Reference application

`Apps/llm_ask.c` is the reference networked native application. It combines `T5NetworkApi`, the firmware keyboard/Wi-Fi handoffs in `T5SystemUiApi`, and the themed text viewport in `T5UiApi` while keeping the LLM service itself entirely outside the firmware image.
