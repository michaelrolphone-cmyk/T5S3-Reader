# RiscRTE Network Host API

> **Specification authority:** Read [RISCRTE_PLATFORM_SPEC.md](RISCRTE_PLATFORM_SPEC.md) and [PLATFORM_CAPABILITY_ROADMAP.md](PLATFORM_CAPABILITY_ROADMAP.md) first. This document primarily describes the **current compatibility ABI**. `T5NetworkApi` and `native` names below are historical implementation identifiers and remain only because the deployed ABI/source has not yet been renamed.

## Canonical direction

RiscRTE networking is a reusable platform service above Wi-Fi and other future network transports. Applications SHOULD request semantic network capabilities and consume reusable DNS, HTTP/HTTPS, WebSocket, time synchronization, discovery, resumable-download, connection-state, and TLS-policy services rather than implementing transport lifecycle independently. Network data movement SHOULD integrate with the RiscRTE stream/pipe architecture where streaming is appropriate.

The current `T5NetworkApi` is a transitional host API. New members must remain append-only for ABI compatibility, but new architectural work should be named and designed as RiscRTE networking/capabilities rather than as a T5-specific facility.

## Current implementation — `T5NetworkApi`

`T5NetworkApi` gives SD-installed RiscRTE ELF applications a small firmware-owned transport layer without embedding application-specific network services in the firmware.

The compatibility header is `lib/NativeApps/include/T5NetworkApi.h`. Request version 1 with:

```c
#include "T5NetworkApi.h"

const t5_network_api_v1 *network = t5_network_get_api(T5_NETWORK_API_VERSION);
if (!network) return;
```

Callers should verify `struct_size` through the last member they require.

### Responsibilities

The current firmware-owned layer provides:

- reporting whether station Wi-Fi is currently connected;
- creating and performing an ESP HTTP client request;
- applying caller-supplied headers;
- temporarily disabling Wi-Fi power saving during the request and restoring the prior mode afterward;
- watchdog servicing while the request is active;
- returning transport error, HTTP status, response length and response-truncated state.

The current application ABI still owns service-specific endpoint/method/authentication/body/serialization/certificate selection, HTTP-status policy, response parsing, retries, and application-level error handling. This describes current implementation, not the final roadmap boundary: reusable connection lifecycle, TLS policy, retries, downloads, and common protocol mechanics should migrate into RiscRTE platform services where they are not inherently application-specific.

For example, `Apps/llm_ask.c` currently owns its LLM7 endpoint, model, prompt, TLS roots and JSON parsing. Provider-specific semantics appropriately remain in the app; generic network mechanics should converge on platform services.

### Connectivity

```c
if (!network->wifi_connected()) {
    // Request the RiscRTE system UI to open its standard Wi-Fi picker.
}
```

Applications that need an interactive Wi-Fi picker currently use the append-only `wifi_request` / `wifi_take_result` members of `T5SystemUiApi`. The firmware unloads the ELF, runs the standard `WifiSelectionActivity`, then relaunches the same ELF with the result available for consumption. This is a compatibility lifecycle and should eventually be expressed through the canonical scene/intent/service model.

### HTTP request

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

`http_request()` returns `true` when the transport completed. A completed request can still have a non-2xx HTTP status, so applications must inspect `result.status_code`. On transport failure, `result.transport_error` contains the underlying `esp_err_t` value.

When the body is larger than the response buffer, the buffer remains NUL-terminated, `result.response_bytes` reports the full observed byte count, and `T5_HTTP_RESPONSE_TRUNCATED` is set in `result.flags`.

### Reference application

`Apps/llm_ask.c` is the current reference networked RiscRTE application. It combines the compatibility `T5NetworkApi`, keyboard/Wi-Fi handoffs in `T5SystemUiApi`, and themed text viewport in `T5UiApi` while keeping provider-specific LLM behavior outside the firmware image.

## Migration requirements

Do not add new app-private infrastructure for generic DNS, TLS policy, download management, connection lifecycle, or stream buffering. Extend or introduce RiscRTE platform services/capabilities and streams, preserve the `T5*` ABI only as a compatibility surface, and document the migration here until old applications no longer require it.