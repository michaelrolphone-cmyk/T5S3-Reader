/* App-facing semantic serial API. Diagnostics come from the selected provider
 * through the generic registry, never from USB-tagged logs or a USB API call. */
#include <T5SerialPortApi.h>
#include <cstddef>
#include <cstdio>

#define t5_serial_port_get_api t5_serial_port_get_api_original
#include "NativeSerialPortBridge_implementation.inc"
#undef t5_serial_port_get_api

namespace {
t5_serial_diagnostic_t acquisitionDiagnostic{};
bool acquisitionFailed = false;
uint32_t diagnosticOwner = 0;
t5_serial_port_api_v1 diagnosticApi{};

t5_serial_result_t diagnosticAcquire(const t5_serial_port_request_t* request,
                                     t5_serial_port_lease_t* lease,
                                     t5_stream_t* rx, t5_stream_t* tx) {
    acquisitionDiagnostic = {};
    acquisitionFailed = false;
    diagnosticOwner = 0;
    auto* context = RuntimeResources::ExecutionContext::current();
    const bool eligible = authorized() && context && context->running(context->id());
    const t5_serial_result_t result = api.acquire(request, lease, rx, tx);
    if (result == T5_SERIAL_OK && lease && *lease && rx && *rx && tx && *tx)
        return T5_SERIAL_OK;

    const t5_serial_result_t reported = result == T5_SERIAL_OK ? T5_SERIAL_IO : result;
    // An unauthorized call cannot inherit the previous invocation's failure
    // record, even if it retained the function-table pointer across unload.
    if (!eligible) return reported;
    acquisitionFailed = true;
    diagnosticOwner = context->id();
    acquisitionDiagnostic.result = reported;
    RuntimeSerial::Registry::Failure failure{};
    const bool recorded = result != T5_SERIAL_OK && providers.lastFailure(&failure);
    acquisitionDiagnostic.provider_error = recorded ? failure.diagnostic.provider_error : 0;
    const char* provider = recorded && failure.provider[0] ? failure.provider : "none";
    const char* detail = recorded && failure.diagnostic.detail[0]
        ? failure.diagnostic.detail : "No structured provider detail available";
    std::snprintf(acquisitionDiagnostic.detail, sizeof(acquisitionDiagnostic.detail),
                  "acquire rc=%ld; provider=%s; provider rc=%ld; %s",
                  static_cast<long>(reported), provider,
                  static_cast<long>(acquisitionDiagnostic.provider_error),
                  result == T5_SERIAL_OK ? "Provider returned incomplete lease or streams" : detail);
    return reported;
}

bool lastAcquisitionDiagnostic(t5_serial_diagnostic_t* out) {
    if (!out || !acquisitionFailed || !diagnosticOwner || !authorized()) return false;
    auto* context = RuntimeResources::ExecutionContext::current();
    if (!context || context->id() != diagnosticOwner ||
        !context->running(diagnosticOwner)) return false;
    *out = acquisitionDiagnostic;
    return true;
}
} // namespace

extern "C" const t5_serial_port_api_v1* t5_serial_port_get_api(uint32_t version) {
    const t5_serial_port_api_v1* original = t5_serial_port_get_api_original(version);
    if (!original) return nullptr;
    diagnosticApi = *original;
    diagnosticApi.acquire = diagnosticAcquire;
    diagnosticApi.last_diagnostic = lastAcquisitionDiagnostic;
    return &diagnosticApi;
}
