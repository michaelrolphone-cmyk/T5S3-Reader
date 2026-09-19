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
// Privileged ELF printf is captured by the generic firmware logger during
// relocation. Report the root physical failure before generic graph errors.
// Only bounded startup diagnostics are selected: never USB RX/TX payloads.
void recentProviderFailures(char* primary, size_t primaryCapacity,
                            char* secondary, size_t secondaryCapacity) {
    primary[0] = 0;
    secondary[0] = 0;
    char power[112]{};
    char controller[88]{};
    char module[112]{};
    char usbStage[80]{};
    const std::string logs = getLastLogs();
    for (size_t pos = 0; pos < logs.size();) {
        const size_t end = logs.find('\n', pos);
        const size_t stop = end == std::string::npos ? logs.size() : end;
        char line[256]{};
        size_t count = stop - pos;
        if (count >= sizeof(line)) count = sizeof(line) - 1u;
        std::memcpy(line, logs.data() + pos, count);
        // Do not attribute a prior failed retry to this acquisition.
        if (std::strstr(line, "USBREF stage=serial-start-enter")) {
            power[0] = controller[0] = module[0] = usbStage[0] = 0;
        }
        const char* vbus = std::strstr(line, "VBUSREF failure=");
        const char* usbController = std::strstr(line, "USBCTRL start-failed");
        const char* provider = std::strstr(line, "PROVREF id=");
        const char* usb = std::strstr(line, "USBREF stage=");
        if (vbus) std::snprintf(power, sizeof(power), "%s", vbus);
        if (usbController) std::snprintf(controller, sizeof(controller), "%s", usbController);
        if (provider && std::strstr(provider, "failure="))
            std::snprintf(module, sizeof(module), "%s", provider);
        if (usb && (std::strstr(usb, "failed") || std::strstr(usb, "quarantined") ||
                    std::strstr(usb, "timeout")))
            std::snprintf(usbStage, sizeof(usbStage), "%s", usb);
        pos = stop == logs.size() ? stop : stop + 1u;
    }
    const char* physical = power[0] ? power : module[0] ? module :
                           "No module failure reported";
    const char* stage = controller[0] ? controller : usbStage;
    std::snprintf(primary, primaryCapacity, "%s", physical);
    if (stage[0]) std::snprintf(secondary, secondaryCapacity, "%s", stage);
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
    char physical[112]{};
    char stage[88]{};
    if (result == T5_SERIAL_IO)
        recentProviderFailures(physical, sizeof(physical), stage, sizeof(stage));
    std::snprintf(acquisitionDiagnostic.detail, sizeof(acquisitionDiagnostic.detail),
                  "acquire rc=%ld; provider rc=%ld\n%s%s%s%s",
                  static_cast<long>(result),
                  static_cast<long>(acquisitionDiagnostic.provider_error),
                  physical[0] ? physical : "No module failure reported",
                  stage[0] ? "\n" : "",
                  stage[0] ? stage : "",
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
