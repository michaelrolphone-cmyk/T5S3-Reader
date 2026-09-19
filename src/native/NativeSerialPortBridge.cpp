/* Preserve the existing serial provider/lease implementation and attach a
 * bounded, transport-neutral diagnostic to failed acquisitions. No hardware
 * operations move into this bridge: the installed ELFs still own the USB and
 * power sequence. */
#include <T5SerialPortApi.h>
#include <cstddef>
#include <cstdio>
#include <cstring>
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include <Logging.h>
#include <string>
#endif

#define t5_serial_port_get_api t5_serial_port_get_api_original
#include "NativeSerialPortBridge_implementation.inc"
#undef t5_serial_port_get_api

namespace {
t5_serial_diagnostic_t acquisitionDiagnostic{};
bool acquisitionFailed = false;
t5_serial_port_api_v1 diagnosticApi{};

#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
// The existing firmware log ring records PROVREF and USBREF failure messages.
// Select only bounded system diagnostics; never return serial RX/TX payloads.
void recentProviderFailures(char* module, size_t moduleCapacity,
                            char* usbStage, size_t stageCapacity) {
    module[0] = 0;
    usbStage[0] = 0;
    const std::string logs = getLastLogs();
    for (size_t pos = 0; pos < logs.size();) {
        const size_t end = logs.find('\n', pos);
        const size_t stop = end == std::string::npos ? logs.size() : end;
        char line[256]{};
        size_t count = stop - pos;
        if (count >= sizeof(line)) count = sizeof(line) - 1u;
        std::memcpy(line, logs.data() + pos, count);
        const char* provider = std::strstr(line, "PROVREF id=");
        const char* usb = std::strstr(line, "USBREF stage=");
        if (provider && std::strstr(provider, "failure="))
            std::snprintf(module, moduleCapacity, "%s", provider);
        if (usb && (std::strstr(usb, "failed") || std::strstr(usb, "quarantined") ||
                    std::strstr(usb, "timeout")))
            std::snprintf(usbStage, stageCapacity, "%s", usb);
        pos = stop == logs.size() ? stop : stop + 1u;
    }
}
#endif

t5_serial_result_t diagnosticAcquire(const t5_serial_port_request_t* request,
                                     t5_serial_port_lease_t* lease,
                                     t5_stream_t* rx, t5_stream_t* tx) {
    acquisitionDiagnostic = {};
    acquisitionFailed = false;
    const t5_serial_result_t result = api.acquire(request, lease, rx, tx);
    if (result == T5_SERIAL_OK && lease && *lease && rx && *rx && tx && *tx)
        return result;
    acquisitionFailed = true;
    acquisitionDiagnostic.result = result;
    const t5_usb_api_v1* usbProvider = t5_usb_get_api(T5_USB_API_VERSION);
    t5_usb_serial_state_t usbState{};
    if (result == T5_SERIAL_IO && usbProvider && usbProvider->serial_read_state &&
        usbProvider->serial_read_state(&usbState))
        acquisitionDiagnostic.provider_error = usbState.last_error;
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
    char module[128]{};
    char usbStage[84]{};
    if (result == T5_SERIAL_IO) recentProviderFailures(module, sizeof(module),
                                                       usbStage, sizeof(usbStage));
    std::snprintf(acquisitionDiagnostic.detail, sizeof(acquisitionDiagnostic.detail),
                  "acquire rc=%ld; provider rc=%ld\n%s%s%s%s",
                  static_cast<long>(result),
                  static_cast<long>(acquisitionDiagnostic.provider_error),
                  module[0] ? module : "No module failure reported",
                  usbStage[0] ? "\n" : "",
                  usbStage[0] ? usbStage : "",
                  result == T5_SERIAL_OK ? "\nMissing lease or streams" : "");
#else
    std::snprintf(acquisitionDiagnostic.detail, sizeof(acquisitionDiagnostic.detail),
                  "acquire rc=%ld; provider rc=%ld%s",
                  static_cast<long>(result),
                  static_cast<long>(acquisitionDiagnostic.provider_error),
                  result == T5_SERIAL_OK ? "; missing lease or streams" : "");
#endif
    return result;
}

bool lastAcquisitionDiagnostic(t5_serial_diagnostic_t* out) {
    if (!out || !acquisitionFailed) return false;
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
