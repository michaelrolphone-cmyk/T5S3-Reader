#include <T5AppApi.h>
#include <T5UsbApi.h>
#include "NativeUsbDeviceRegistry.h"
#include "runtime/drivers/InstalledProviderGraph.h"
#include <RiscUsbControllerV1.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

// Compatibility-only translation of T5UsbApi to the installed provider graph.
// There is NO IDF USB host, charger, I2C, VBUS, DMA, device enumeration, class
// descriptor parser, hardware worker, or peripheral ownership in firmware.
// The installed ELFs implement i2c.bus -> board.power.vbus -> usb.controller
// -> usb.host -> serial.port. The runtime only holds opaque, versioned grants.
namespace {
using RuntimeInstalledProviders::Lease;
constexpr uint32_t kMaxTransfer = 512;
constexpr uint32_t kReadTimeoutMs = 10;
constexpr uint32_t kWriteTimeoutMs = 100;
constexpr uint32_t kMutexTimeoutMs = 250;
SemaphoreHandle_t mutex = nullptr;
Lease hostGrant{};
Lease classGrant{};
const risc_usb_host_discovery_v1* host = nullptr;
const risc_usb_cdc_api_v1* serial = nullptr;
uint64_t device = 0;
uint64_t session = 0;
uint64_t unboundToken = 0;
uint64_t pendingConfigurationToken = 0;
int32_t lastDeviceCount = -1;
bool running = false;
bool quarantined = false;
bool debugConsoleSuspended = false;
t5_usb_serial_state_t state{};
// reconcile() is always invoked with the bridge mutex held. Keeping the USB
// descriptor workspace static avoids putting 4096 bytes on loopTask's stack.
uint8_t configurationDescriptor[RISC_USB_CONFIG_LIMIT]{};

// Arduino's boot debug CDC and the USB host share the internal PHY on this
// board. The known-good v1.2.16 firmware released debug CDC and waited 20 ms
// BEFORE powering VBUS or installing the host. This is only boot-console
// arbitration: the physical host, PHY and USB device operations stay in ELFs.
// Never re-enable the debug interface while an ELF may still own the PHY.
void suspendDebugConsole() {
#if defined(ENABLE_SERIAL_LOG) && defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE && \
    defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
    if (!debugConsoleSuspended) {
        LOG_INF("USB", "USBREF stage=debug-console-release");
        Serial.end();
        delay(20);
        debugConsoleSuspended = true;
        LOG_INF("USB", "USBREF stage=debug-console-released");
    }
#endif
}
void restoreDebugConsole() {
#if defined(ENABLE_SERIAL_LOG) && defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE && \
    defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
    if (debugConsoleSuspended) {
        delay(20);
        Serial.begin(115200);
        debugConsoleSuspended = false;
        LOG_INF("USB", "USBREF stage=debug-console-restored");
    }
#endif
}

bool active() { return t5_app_get_api(T5_APP_ABI_VERSION) != nullptr; }
bool initialize() {
    if (!mutex) mutex = xSemaphoreCreateMutex();
    return mutex != nullptr;
}
struct Lock {
    bool acquired;
    Lock() : acquired(xSemaphoreTake(mutex, pdMS_TO_TICKS(kMutexTimeoutMs)) == pdTRUE) {}
    ~Lock() { if (acquired) xSemaphoreGive(mutex); }
    explicit operator bool() const { return acquired; }
};
void initialState(uint8_t status) {
    state = {};
    state.status = status;
    state.line_coding = {115200u, 8u, T5_USB_PARITY_NONE, 1u, 0u};
}
void error(int32_t code) {
    state.status = T5_USB_STATUS_ERROR;
    state.last_error = code;
    state.connected = 0;
    nativeUsbProviderDetach();
}
// Failed activation or a rejected host ABI can leave a partially started ELF.
// Only reclaim the boot debug PHY after graph shutdown proves quiescence.
bool restoreAfterSafeShutdown() {
    if (!RuntimeInstalledProviders::shutdown()) {
        quarantined = true;
        LOG_ERR("USB", "USBREF stage=debug-console-restore-denied reason=unsafe-provider-shutdown");
        return false;
    }
    restoreDebugConsole();
    return true;
}
bool codingValid(const t5_usb_line_coding_t* coding) {
    return coding && coding->baud_rate >= 300 && coding->baud_rate <= 3000000 &&
        coding->data_bits >= 5 && coding->data_bits <= 8 &&
        coding->parity <= T5_USB_PARITY_SPACE &&
        (coding->stop_bits == 1 || coding->stop_bits == 2);
}
bool classApiValid(const risc_usb_cdc_api_v1* api) {
    return api && api->api_version == RISC_USB_CDC_API_V1 &&
        api->struct_size >= sizeof(risc_usb_cdc_api_v1) && api->open &&
        api->configure && api->control_lines && api->read && api->write && api->close;
}
bool hostApiValid(const risc_usb_host_discovery_v1* api) {
    return api && api->host.api_version == RISC_USB_HOST_API_V1 &&
        api->host.struct_size >= sizeof(risc_usb_host_discovery_v1) &&
        api->poll && api->devices && api->host.configuration;
}

// A failed ELF release may leave DMA or callback ownership outstanding.
// Do not unmap it, unpin its package, or silently choose the compiled host.
bool closeClass() {
    if (!session) return true;
    if (!serial || !serial->close(session)) {
        quarantined = true;
        error(-1201);
        return false;
    }
    session = 0;
    device = 0;
    serial = nullptr;
    state.connected = 0;
    nativeUsbProviderDetach();
    if (classGrant.grant.slot && !RuntimeInstalledProviders::release(&classGrant)) {
        quarantined = true;
        error(-1202);
        return false;
    }
    return true;
}

// A class ELF examines the actual descriptors and VID/PID; the firmware does
// not make chip-specific matching decisions. Probe each installable class by
// stable package ID, never fall back to a resident hardware implementation.
bool openClass(uint64_t token, uint16_t vid, uint16_t pid) {
    const char* choices[] = {"usb-ch34x-v2", "usb-cdc-acm-v2", "usb-cp210x-v2"};
    bool foundProvider = false;
    for (const char* id : choices) {
        Lease grant{};
        if (!RuntimeInstalledProviders::acquire(id, "serial.port", 1, &grant)) continue;
        foundProvider = true;
        const auto* api = static_cast<const risc_usb_cdc_api_v1*>(grant.interface);
        if (!classApiValid(api)) {
            LOG_ERR("USB", "USBREF stage=serial-class-abi-invalid provider=%s", id);
            if (!RuntimeInstalledProviders::release(&grant)) quarantined = true;
            if (quarantined) return false;
            continue;
        }
        const uint64_t opened = api->open(token);
        if (opened && api->configure(opened, state.line_coding.baud_rate,
                state.line_coding.data_bits, state.line_coding.parity,
                state.line_coding.stop_bits) &&
            api->control_lines(opened, state.dtr != 0, state.rts != 0)) {
            classGrant = grant;
            serial = api;
            session = opened;
            device = token;
            unboundToken = 0;
            pendingConfigurationToken = 0;
            state.vid = vid;
            state.pid = pid;
            state.connected = 1;
            state.status = T5_USB_STATUS_READY;
            state.last_error = 0;
            std::snprintf(state.product, sizeof(state.product),
                          "%s %04X:%04X", id, static_cast<unsigned>(vid),
                          static_cast<unsigned>(pid));
            nativeUsbProviderAttach(&state, 0xff);
            LOG_INF("USB", "USBREF provider=%s state=bound vid=%04X pid=%04X",
                    id, static_cast<unsigned>(vid), static_cast<unsigned>(pid));
            return true;
        }
        if (opened && !api->close(opened)) {
            // Retain the grant, executable mapping and package pin on an
            // uncertain physical teardown; never reassign this interface.
            classGrant = grant;
            serial = api;
            session = opened;
            device = token;
            quarantined = true;
            error(-1203);
            return false;
        }
        if (!RuntimeInstalledProviders::release(&grant)) {
            quarantined = true;
            error(-1204);
            return false;
        }
    }
    LOG_ERR("USB", "USBREF stage=serial-class-unbound vid=%04X pid=%04X provider-installed=%u",
            static_cast<unsigned>(vid), static_cast<unsigned>(pid),
            static_cast<unsigned>(foundProvider));
    return false;
}

// Poll on the serialized provider executor, not in a USB callback. Only
// generation-qualified device tokens supplied by the host ELF are retained.
void reconcile() {
    if (!running || quarantined || !host) return;
    size_t processed = 0;
    if (!host->poll(host->host.context, 16, &processed)) {
        LOG_ERR("USB", "USBREF stage=host-poll-failed");
        quarantined = true;
        error(-1210);
        return;
    }
    uint64_t devices[RISC_USB_HOST_MAX_DEVICES]{};
    size_t count = RISC_USB_HOST_MAX_DEVICES;
    if (!host->devices(host->host.context, devices, &count) || count > RISC_USB_HOST_MAX_DEVICES) {
        LOG_ERR("USB", "USBREF stage=host-devices-failed");
        quarantined = true;
        error(-1211);
        return;
    }
    if (lastDeviceCount != static_cast<int32_t>(count)) {
        lastDeviceCount = static_cast<int32_t>(count);
        LOG_INF("USB", "USBREF stage=host-devices count=%u events=%u",
                static_cast<unsigned>(count), static_cast<unsigned>(processed));
    }
    if (session) {
        bool present = false;
        for (size_t i = 0; i < count; ++i)
            if (devices[i] == device) { present = true; break; }
        if (!present) {
            if (!closeClass()) return;
            state.status = T5_USB_STATUS_WAITING;
        } else return;
    }
    if (!count) {
        unboundToken = pendingConfigurationToken = 0;
        state.status = T5_USB_STATUS_WAITING;
        state.last_error = 0;
        return;
    }
    bool missingConfiguration = false;
    bool failedClass = false;
    for (size_t i = 0; i < count && !quarantined; ++i) {
        if (devices[i] == unboundToken) {
            failedClass = true;
            continue; // Do not repeatedly execute vendor init for a rejected device.
        }
        std::memset(configurationDescriptor, 0, sizeof(configurationDescriptor));
        size_t length = sizeof(configurationDescriptor);
        uint16_t vid = 0, pid = 0;
        if (!host->host.configuration(host->host.context, devices[i],
                                      configurationDescriptor, &length, &vid, &pid)) {
            missingConfiguration = true;
            if (pendingConfigurationToken != devices[i]) {
                pendingConfigurationToken = devices[i];
                LOG_ERR("USB", "USBREF stage=configuration-unavailable token=%llu",
                        static_cast<unsigned long long>(devices[i]));
            }
            continue; // Enumeration may still be completing: retry on next poll.
        }
        pendingConfigurationToken = 0;
        LOG_INF("USB", "USBREF stage=configuration-ready vid=%04X pid=%04X length=%u",
                static_cast<unsigned>(vid), static_cast<unsigned>(pid),
                static_cast<unsigned>(length));
        if (openClass(devices[i], vid, pid)) return;
        if (quarantined) return;
        unboundToken = devices[i];
        failedClass = true;
    }
    if (quarantined) return;
    if (failedClass) {
        state.status = T5_USB_STATUS_ERROR;
        state.last_error = -1240; // Device enumerated but no class ELF could bind.
    } else if (missingConfiguration) {
        state.status = T5_USB_STATUS_ERROR;
        state.last_error = -1241; // USB attached, configuration not yet readable.
    } else {
        state.status = T5_USB_STATUS_WAITING;
        state.last_error = 0;
    }
}

// This is a CHEAP hardware-platform predicate, not an installed-package
// integrity result. Calls from provider selection and stream creation must
// never rescan SD. Graph admission independently validates the complete
// installed package inventory, requirements, exact imports and ELF bytes.
bool supported() {
#ifdef BOARD_T5S3_PRO
    return Storage.ready();
#else
    return false;
#endif
}
bool serialStart(const t5_usb_line_coding_t* coding) {
    if (!active() || !codingValid(coding) || !initialize()) return false;
    LOG_INF("USB", "USBREF stage=serial-start-enter");
    Lock lock;
    if (!lock) {
        LOG_ERR("USB", "USBREF stage=serial-start-lock-timeout");
        return false;
    }
    if (quarantined) {
        LOG_ERR("USB", "USBREF stage=serial-start-quarantined");
        return false;
    }
    if (running) {
        state.line_coding = *coding;
        return !session || (serial && serial->configure(session, coding->baud_rate,
                    coding->data_bits, coding->parity, coding->stop_bits));
    }
    // Release the boot debug console BEFORE any provider may power VBUS or
    // usb_host_install() allocates the shared internal PHY/interrupt. Restoring
    // it later is conditional on a fully quiescent graph, not merely on an
    // unsuccessful acquire() result.
    suspendDebugConsole();
    LOG_INF("USB", "USBREF stage=host-acquire-begin");
    if (!RuntimeInstalledProviders::acquire(
            "usb-host-v2", "usb.host", 1, &hostGrant)) {
        LOG_ERR("USB", "USBREF stage=host-acquire-failed");
        error(-1220);
        (void)restoreAfterSafeShutdown();
        return false;
    }
    LOG_INF("USB", "USBREF stage=host-acquired");
    host = static_cast<const risc_usb_host_discovery_v1*>(hostGrant.interface);
    if (!hostApiValid(host)) {
        LOG_ERR("USB", "USBREF stage=host-api-invalid");
        if (!RuntimeInstalledProviders::release(&hostGrant)) quarantined = true;
        host = nullptr;
        error(-1221);
        if (!quarantined) (void)restoreAfterSafeShutdown();
        return false;
    }
    initialState(T5_USB_STATUS_WAITING);
    state.line_coding = *coding;
    unboundToken = pendingConfigurationToken = 0;
    lastDeviceCount = -1;
    running = true;
    LOG_INF("USB", "USBREF stage=initial-reconcile");
    reconcile();
    if (quarantined) return false;
    LOG_INF("USB", "USBREF state=host-active source=installed-elf");
    return true; // Physical enumeration remains asynchronous.
}
void serialStop() {
    if (!initialize()) return;
    Lock lock;
    if (!lock) { LOG_ERR("USB", "USBREF stage=serial-stop-lock-timeout"); return; }
    nativeUsbProviderDetach();
    if (quarantined) return; // Do not unload on a failed physical teardown.
    if (!closeClass()) return;
    if (hostGrant.grant.slot && !RuntimeInstalledProviders::release(&hostGrant)) {
        quarantined = true;
        error(-1230);
        return;
    }
    host = nullptr;
    if (!RuntimeInstalledProviders::shutdown()) {
        quarantined = true;
        error(-1231);
        return;
    }
    running = false;
    unboundToken = pendingConfigurationToken = 0;
    lastDeviceCount = -1;
    initialState(T5_USB_STATUS_OFF);
    restoreDebugConsole();
    LOG_INF("USB", "USBREF state=stopped source=installed-elf");
}
bool serialReadState(t5_usb_serial_state_t* out) {
    if (!out || !initialize()) return false;
    Lock lock;
    if (!lock) return false;
    reconcile();
    *out = state;
    return true;
}
bool serialSetLineCoding(const t5_usb_line_coding_t* coding) {
    if (!codingValid(coding) || !initialize()) return false;
    Lock lock;
    if (!lock) return false;
    if (quarantined) return false;
    if (session && (!serial || !serial->configure(session, coding->baud_rate,
                coding->data_bits, coding->parity, coding->stop_bits))) return false;
    state.line_coding = *coding;
    return true;
}
bool serialSetControlLines(bool dtr, bool rts) {
    if (!initialize()) return false;
    Lock lock;
    if (!lock) return false;
    if (quarantined) return false;
    if (session && (!serial || !serial->control_lines(session, dtr, rts))) return false;
    state.dtr = dtr;
    state.rts = rts;
    return true;
}
size_t serialRead(uint8_t* bytes, size_t capacity) {
    if (!bytes || !capacity || !initialize()) return 0;
    Lock lock;
    if (!lock) return 0;
    reconcile();
    if (!session || !serial || quarantined) return 0;
    const size_t n = std::min<size_t>(capacity, kMaxTransfer);
    const int32_t received = serial->read(session, bytes, n, kReadTimeoutMs);
    if (received > 0 && static_cast<size_t>(received) <= n) {
        state.rx_bytes += static_cast<uint32_t>(received);
        return static_cast<size_t>(received);
    }
    // A timed-out bulk IN is no data, not a reason to discard an active
    // device. A host detach is separately detected through reconcile().
    return 0;
}
size_t serialWrite(const uint8_t* bytes, size_t length) {
    if (!bytes || !length || !initialize()) return 0;
    Lock lock;
    if (!lock) return 0;
    reconcile();
    if (!session || !serial || quarantined) return 0;
    const size_t n = std::min<size_t>(length, kMaxTransfer);
    const int32_t sent = serial->write(session, bytes, n, kWriteTimeoutMs);
    if (sent > 0 && static_cast<size_t>(sent) <= n) {
        state.tx_bytes += static_cast<uint32_t>(sent);
        return static_cast<size_t>(sent);
    }
    return 0;
}
const t5_usb_api_v1 api = {
    T5_USB_API_VERSION, sizeof(t5_usb_api_v1), supported, serialStart,
    serialStop, serialReadState, serialSetLineCoding, serialSetControlLines,
    serialRead, serialWrite,
};
} // namespace

extern "C" const t5_usb_api_v1* t5_usb_get_api(uint32_t version) {
    return version == T5_USB_API_VERSION && active() ? &api : nullptr;
}
