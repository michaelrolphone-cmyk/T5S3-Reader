#include <T5AppApi.h>
#include <T5UsbApi.h>
#include "NativeUsbClassBridge.h"
#include "NativeUsbDeviceRegistry.h"
#include "runtime/drivers/InstalledProviderGraph.h"
#include <RiscUsbControllerV1.h>
#include <HalStorage.h>
#include <Logging.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

// Compatibility-only T5UsbApi surface. The installed ELFs own the controller,
// host, device class and physical transfers; no firmware USB data plane.
namespace {
using RuntimeInstalledProviders::Lease;
constexpr uint32_t kMaxTransfer = 512;
constexpr uint32_t kMutexTimeoutMs = 250;
SemaphoreHandle_t mutex = nullptr;
Lease hostGrant{};
const risc_usb_host_discovery_v1* host = nullptr;
uint64_t device = 0;
uint64_t session = 0;
bool running = false;
bool quarantined = false;
t5_usb_serial_state_t state{};
uint8_t configurationDescriptor[RISC_USB_CONFIG_LIMIT]{};

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
bool codingValid(const t5_usb_line_coding_t* coding) {
    return coding && coding->baud_rate >= 300 && coding->baud_rate <= 3000000 &&
        coding->data_bits >= 5 && coding->data_bits <= 8 &&
        coding->parity <= T5_USB_PARITY_SPACE &&
        (coding->stop_bits == 1 || coding->stop_bits == 2);
}
t5_serial_config_t classCoding(const t5_usb_line_coding_t& coding) {
    return {coding.baud_rate, coding.data_bits, coding.parity, coding.stop_bits,
            T5_SERIAL_FLOW_NONE};
}
bool hostApiValid(const risc_usb_host_discovery_v1* api) {
    return api && api->host.api_version == RISC_USB_HOST_API_V1 &&
        api->host.struct_size >= sizeof(risc_usb_host_discovery_v1) &&
        api->poll && api->devices && api->host.configuration;
}

// A failed hardware close keeps the class ELF and its package generation
// pinned; never select another provider while a token is unquiesced.
bool closeClass() {
    if (!session) return true;
    if (nativeUsbClassToken() != session || !nativeUsbClassUnbindChecked()) {
        quarantined = true;
        error(-1201);
        return false;
    }
    session = 0;
    device = 0;
    state.connected = 0;
    nativeUsbProviderDetach();
    return true;
}

void connected(const char* id, uint64_t token, uint16_t vid, uint16_t pid) {
    device = token;
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
}

bool openBoundClass(uint64_t token, uint16_t vid, uint16_t pid) {
    if (nativeUsbClassToken()) return false;
    if (!nativeUsbClassEnsureInstalled() || !nativeUsbClassAvailable()) return false;
    nativeUsbClassObserveDevice(token);
    if (!nativeUsbClassStart(classCoding(state.line_coding))) {
        // Failed configure/close may leave a mapped ELF with an outstanding
        // token. Never enumerate or bind another class in that condition.
        if (nativeUsbClassToken()) {
            quarantined = true;
            error(-1205);
        }
        return false;
    }
    if (!nativeUsbClassControl(state.dtr != 0, state.rts != 0)) {
        if (!nativeUsbClassUnbindChecked()) {
            quarantined = true;
            error(-1206);
        }
        return false;
    }
    session = nativeUsbClassToken();
    if (!session) return false;
    connected("installed-class-elf", token, vid, pid);
    return true;
}

// A provider's open(device) determines whether it handles the device. A
// lazily prebound class is first cleanly released so the new scan probes every
// candidate exactly once. Binding/start failures are NOT ordinary no-match.
bool openClass(uint64_t token, uint16_t vid, uint16_t pid) {
    if (nativeUsbClassToken()) return false;
    if (nativeUsbClassBound() && !nativeUsbClassUnbindChecked()) {
        quarantined = true;
        error(-1204);
        return false;
    }
    size_t cursor = 0;
    bool faulted = false;
    while (nativeUsbClassBindNextInstalled(&cursor, &faulted)) {
        if (openBoundClass(token, vid, pid)) return true;
        if (quarantined || nativeUsbClassToken() ||
            !nativeUsbClassUnbindChecked()) {
            quarantined = true;
            error(-1204);
            return false;
        }
    }
    if (faulted) {
        quarantined = true;
        error(-1207);
    }
    return false;
}

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
    if (session) {
        bool present = false;
        for (size_t i = 0; i < count; ++i)
            if (devices[i] == device) { present = true; break; }
        if (!present) {
            if (!closeClass()) return;
            state.status = T5_USB_STATUS_WAITING;
        } else return;
    }
    for (size_t i = 0; i < count && !quarantined; ++i) {
        std::memset(configurationDescriptor, 0, sizeof(configurationDescriptor));
        size_t length = sizeof(configurationDescriptor);
        uint16_t vid = 0, pid = 0;
        if (!host->host.configuration(host->host.context, devices[i],
                                      configurationDescriptor, &length, &vid, &pid)) continue;
        if (openClass(devices[i], vid, pid)) return;
    }
    if (!quarantined) state.status = T5_USB_STATUS_WAITING;
}

bool supported() {
#ifdef BOARD_T5S3_PRO
    return Storage.ready();
#else
    return false;
#endif
}
bool configureSession(const t5_usb_line_coding_t* coding) {
    return !session || (nativeUsbClassToken() == session &&
                        nativeUsbClassConfigure(classCoding(*coding)));
}
bool serialStart(const t5_usb_line_coding_t* coding) {
    if (!active() || !codingValid(coding) || !initialize()) return false;
    Lock lock;
    if (!lock || quarantined) return false;
    if (running) {
        if (!configureSession(coding)) return false;
        state.line_coding = *coding;
        return true;
    }
    // Resolve the verified host capability; never assume a package identity.
    char hostId[96]{}, alternate[96]{};
    size_t cursor = 0;
    if (!RuntimeInstalledProviders::nextProvider("usb.host", 1, &cursor,
                                                hostId, sizeof(hostId)) ||
        RuntimeInstalledProviders::nextProvider("usb.host", 1, &cursor,
                                                alternate, sizeof(alternate)) ||
        !RuntimeInstalledProviders::acquire(hostId, "usb.host", 1, &hostGrant)) {
        error(-1220);
        return false;
    }
    host = static_cast<const risc_usb_host_discovery_v1*>(hostGrant.interface);
    if (!hostApiValid(host)) {
        if (!RuntimeInstalledProviders::release(&hostGrant)) quarantined = true;
        host = nullptr;
        error(-1221);
        return false;
    }
    initialState(T5_USB_STATUS_WAITING);
    state.line_coding = *coding;
    running = true;
    reconcile();
    if (quarantined) return false;
    LOG_INF("USB", "USBREF state=host-active source=installed-elf");
    return true;
}
void serialStop() {
    if (!initialize()) return;
    Lock lock;
    if (!lock) { LOG_ERR("USB", "USBREF stage=serial-stop-lock-timeout"); return; }
    nativeUsbProviderDetach();
    if (quarantined) return;
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
    initialState(T5_USB_STATUS_OFF);
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
    if (!lock || quarantined || !configureSession(coding)) return false;
    state.line_coding = *coding;
    return true;
}
bool serialSetControlLines(bool dtr, bool rts) {
    if (!initialize()) return false;
    Lock lock;
    if (!lock || quarantined) return false;
    if (session && (nativeUsbClassToken() != session ||
                    !nativeUsbClassControl(dtr, rts))) return false;
    state.dtr = dtr;
    state.rts = rts;
    return true;
}
size_t serialRead(uint8_t* bytes, size_t capacity) {
    if (!bytes || !capacity || !initialize()) return 0;
    Lock lock;
    if (!lock) return 0;
    reconcile();
    if (!session || quarantined || nativeUsbClassToken() != session) return 0;
    const size_t n = std::min<size_t>(capacity, kMaxTransfer);
    uint32_t received = 0;
    if (nativeUsbClassRead(bytes, static_cast<uint32_t>(n), &received) == T5_STREAM_OK &&
        received <= n) {
        state.rx_bytes += received;
        return received;
    }
    return 0;
}
size_t serialWrite(const uint8_t* bytes, size_t length) {
    if (!bytes || !length || !initialize()) return 0;
    Lock lock;
    if (!lock) return 0;
    reconcile();
    if (!session || quarantined || nativeUsbClassToken() != session) return 0;
    const size_t n = std::min<size_t>(length, kMaxTransfer);
    uint32_t sent = 0;
    if (nativeUsbClassWrite(bytes, static_cast<uint32_t>(n), &sent) == T5_STREAM_OK &&
        sent <= n) {
        state.tx_bytes += sent;
        return sent;
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
