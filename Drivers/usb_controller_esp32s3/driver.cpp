/* Experimental PHYSICAL USB controller ELF. Nothing here forwards USB into
 * NativeUsbBridge or a resident firmware USB driver. Build must link the IDF
 * USB host implementation INTO this ELF and resolve only generic OS/CPU ports.
 * All calls (including next_event) must be serialized on one provider executor;
 * the sole IDF callback executes in that same context. Not a mock. */
#include "RiscUsbControllerV1.h"
#include "RiscUsbVbusV1.h"
#include <usb/usb_host.h>
#include <esp_intr_alloc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <cstdint>
#include <cstdio>

namespace {
constexpr size_t kEvents = 16;
constexpr size_t kBuffer = RISC_USB_CONFIG_LIMIT + 8;
constexpr uint32_t kTeardownTicks = 500;
struct Device {
    usb_device_handle_t handle;
    uint64_t token;
    uint16_t vid, pid;
    bool attached;
};
struct Claim {
    uint64_t token, physical_device;
    uint8_t number, alternate;
};
struct Event {
    uint8_t kind, address;
    usb_device_handle_t handle;
};
static const risc_usb_vbus_api_v1 *power;
static uint64_t powerLease, serial;
static bool running, installed, fault;
static usb_host_client_handle_t client;
static usb_transfer_t *transfer;
static bool inFlight, completed;
static Event queue[kEvents];
static size_t queueHead, queueTail, queueCount;
static Device devices[RISC_USB_HOST_MAX_DEVICES];
static Claim claims[RISC_USB_HOST_MAX_CLAIMS];

bool equals(const char *a, const char *b) {
    return a && b && std::strcmp(a, b) == 0;
}
uint64_t token() {
    if (serial == UINT64_MAX) { fault = true; return 0; }
    return ++serial;
}
Device *device(uint64_t id) {
    if (!id || fault) return nullptr;
    for (auto &d : devices) if (d.handle && d.token == id) return &d;
    return nullptr;
}
Claim *claim(uint64_t id) {
    if (!id || fault) return nullptr;
    for (auto &c : claims) if (c.token == id) return &c;
    return nullptr;
}
bool claimed(uint64_t id) {
    for (const auto &c : claims) if (c.token && c.physical_device == id) return true;
    return false;
}
bool enqueue(uint8_t kind, uint8_t address, usb_device_handle_t handle) {
    if (queueCount == kEvents) { fault = true; return false; }
    queue[queueTail] = {kind, address, handle};
    queueTail = (queueTail + 1) % kEvents;
    ++queueCount;
    return true;
}
void client_event(const usb_host_client_event_msg_t *msg, void *) {
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV)
        (void)enqueue(1, msg->new_dev.address, nullptr);
    else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE)
        (void)enqueue(2, 0, msg->dev_gone.dev_hdl);
}
void complete_transfer(usb_transfer_t *) { inFlight = false; completed = true; }
bool pump(TickType_t delay) {
    if (!installed || !client) return false;
    uint32_t flags = 0;
    esp_err_t a = usb_host_lib_handle_events(0, &flags);
    esp_err_t b = usb_host_client_handle_events(client, delay);
    if ((a != ESP_OK && a != ESP_ERR_TIMEOUT) ||
        (b != ESP_OK && b != ESP_ERR_TIMEOUT)) { fault = true; return false; }
    return !fault;
}
bool wait_completion(uint32_t milliseconds) {
    TickType_t begun = xTaskGetTickCount();
    TickType_t budget = pdMS_TO_TICKS(milliseconds);
    if (!budget) budget = 1;
    while (!completed) {
        if (fault || !pump(1)) return false;
        if ((TickType_t)(xTaskGetTickCount() - begun) >= budget) return false;
    }
    return transfer && transfer->status == USB_TRANSFER_STATUS_COMPLETED;
}
/* Never free a submitted transfer: a timed-out operation retains its DMA
 * allocation and prevents unmapping until its callback is delivered. */
bool idle_transfer() {
    if (inFlight) return false;
    if (completed) completed = false;
    return transfer != nullptr;
}

bool endpoint_mps(Device *d, uint8_t iface, uint8_t alt,
                  uint8_t endpoint, uint16_t *mps) {
    const usb_config_desc_t *config = nullptr;
    if (!d || !d->attached || !mps ||
        usb_host_get_active_config_descriptor(d->handle, &config) != ESP_OK ||
        !config || config->wTotalLength < 9 ||
        config->wTotalLength > RISC_USB_CONFIG_LIMIT) return false;
    const auto *bytes = reinterpret_cast<const uint8_t *>(config);
    size_t length = config->wTotalLength;
    bool selected = false;
    for (size_t pos = 0; pos < length;) {
        if (length - pos < 2) return false;
        const uint8_t n = bytes[pos], type = bytes[pos + 1];
        if (n < 2 || n > length - pos) return false;
        if (type == 4) {
            if (n < 9) return false;
            selected = bytes[pos + 2] == iface && bytes[pos + 3] == alt;
        } else if (type == 5 && selected) {
            if (n < 7) return false;
            const uint16_t packet = static_cast<uint16_t>(bytes[pos + 4] |
                                                     (bytes[pos + 5] << 8));
            if (bytes[pos + 2] == endpoint && (bytes[pos + 3] & 3u) == 2u &&
                packet && packet <= 512) { *mps = packet; return true; }
        }
        pos += n;
    }
    return false;
}

int32_t next_event(void *, risc_usb_controller_event_v1 *out) {
    if (!running || !out || fault || !pump(0)) return -1;
    if (!queueCount) return 0;
    Event e = queue[queueHead];
    queueHead = (queueHead + 1) % kEvents;
    --queueCount;
    if (e.kind == 1) {
        Device *freeSlot = nullptr;
        for (auto &d : devices) if (!d.handle) { freeSlot = &d; break; }
        if (!freeSlot) { fault = true; return -1; }
        usb_device_handle_t handle = nullptr;
        if (usb_host_device_open(client, e.address, &handle) != ESP_OK || !handle)
            return -1;
        const usb_device_desc_t *descriptor = nullptr;
        if (usb_host_get_device_descriptor(handle, &descriptor) != ESP_OK ||
            !descriptor) {
            if (usb_host_device_close(client, handle) != ESP_OK) fault = true;
            return -1;
        }
        uint64_t id = token();
        if (!id) { fault = true; return -1; }
        *freeSlot = {handle, id, descriptor->idVendor, descriptor->idProduct, true};
        *out = {1, id};
        return 1;
    }
    for (auto &d : devices) {
        if (d.handle != e.handle) continue;
        if (!d.attached) { fault = true; return -1; }
        d.attached = false;
        *out = {2, d.token};
        return 1;
    }
    fault = true;
    return -1;
}
bool configuration(void *, uint64_t id, uint8_t *bytes, size_t *size,
                   uint16_t *vid, uint16_t *pid) {
    Device *d = device(id);
    if (!running || !d || !d->attached || !bytes || !size || !vid || !pid)
        return false;
    const usb_config_desc_t *descriptor = nullptr;
    if (usb_host_get_active_config_descriptor(d->handle, &descriptor) != ESP_OK ||
        !descriptor || descriptor->wTotalLength < 9 ||
        descriptor->wTotalLength > RISC_USB_CONFIG_LIMIT ||
        *size < descriptor->wTotalLength) return false;
    *size = descriptor->wTotalLength;
    std::memcpy(bytes, descriptor, *size);
    *vid = d->vid;
    *pid = d->pid;
    return true;
}
bool claim_interface(void *, uint64_t id, uint8_t iface, uint8_t alt,
                     uint64_t *out) {
    Device *d = device(id);
    if (!running || !d || !d->attached || !out) return false;
    Claim *slot = nullptr;
    for (auto &c : claims) {
        if (c.token && c.physical_device == id && c.number == iface)
            return false;
        if (!c.token && !slot) slot = &c;
    }
    if (!slot) return false;
    /* Existence of the requested interface/alternate is verified by IDF. */
    if (usb_host_interface_claim(client, d->handle, iface, alt) != ESP_OK)
        return false;
    uint64_t assigned = token();
    if (!assigned) { fault = true; return false; }
    *slot = {assigned, id, iface, alt};
    *out = assigned;
    return true;
}
bool release_interface(void *, uint64_t id) {
    Claim *c = claim(id);
    if (!running || !c || inFlight) return false;
    Device *d = device(c->physical_device);
    if (!d || usb_host_interface_release(client, d->handle, c->number) != ESP_OK)
        return false;
    *c = {};
    if (!d->attached && !claimed(d->token)) {
        if (usb_host_device_close(client, d->handle) != ESP_OK) return false;
        *d = {};
    }
    return true;
}
int32_t control(void *, uint64_t id, uint8_t type, uint8_t request,
                uint16_t value, uint16_t index, uint8_t *payload,
                uint16_t length, uint32_t timeout) {
    Device *d = device(id);
    if (!running || !d || !d->attached || !timeout ||
        length > RISC_USB_CONFIG_LIMIT || (length && !payload) ||
        !idle_transfer()) return -1;
    uint8_t *b = transfer->data_buffer;
    b[0] = type; b[1] = request;
    b[2] = static_cast<uint8_t>(value); b[3] = static_cast<uint8_t>(value >> 8);
    b[4] = static_cast<uint8_t>(index); b[5] = static_cast<uint8_t>(index >> 8);
    b[6] = static_cast<uint8_t>(length); b[7] = static_cast<uint8_t>(length >> 8);
    if ((type & 0x80u) == 0 && length) std::memcpy(b + 8, payload, length);
    transfer->device_handle = d->handle;
    transfer->bEndpointAddress = 0;
    transfer->num_bytes = length + 8;
    transfer->callback = complete_transfer;
    transfer->context = nullptr;
    completed = false;
    inFlight = true;
    if (usb_host_transfer_submit_control(client, transfer) != ESP_OK) {
        inFlight = false; return -1;
    }
    if (!wait_completion(timeout)) return -1;
    int32_t n = transfer->actual_num_bytes;
    if (n >= 8) n -= 8;
    if (n < 0 || n > length) return -1;
    if ((type & 0x80u) && n) std::memcpy(payload, b + 8, n);
    return (type & 0x80u) ? n : length;
}
int32_t bulk(void *, uint64_t id, uint8_t endpoint,
             uint8_t *dst, const uint8_t *src, size_t length,
             uint32_t timeout, bool reading) {
    Claim *c = claim(id);
    Device *d = c ? device(c->physical_device) : nullptr;
    uint16_t mps = 0;
    if (!running || !d || !d->attached || !length ||
        length > RISC_USB_CONFIG_LIMIT || !timeout ||
        (reading ? (!dst || !(endpoint & 0x80u)) : (!src || (endpoint & 0x80u))) ||
        !endpoint_mps(d, c->number, c->alternate, endpoint, &mps) ||
        !idle_transfer()) return -1;
    size_t transaction = length;
    if (reading) {
        transaction = ((length + mps - 1) / mps) * mps;
        if (transaction > RISC_USB_CONFIG_LIMIT) return -1;
    } else std::memcpy(transfer->data_buffer, src, length);
    transfer->device_handle = d->handle;
    transfer->bEndpointAddress = endpoint;
    transfer->num_bytes = static_cast<int>(transaction);
    transfer->callback = complete_transfer;
    transfer->context = nullptr;
    completed = false;
    inFlight = true;
    if (usb_host_transfer_submit(transfer) != ESP_OK) {
        inFlight = false; return -1;
    }
    if (!wait_completion(timeout)) return -1;
    int32_t n = transfer->actual_num_bytes;
    if (n < 0 || static_cast<size_t>(n) > length) return -1;
    if (reading && n) std::memcpy(dst, transfer->data_buffer, n);
    return n;
}
int32_t bulk_read(void *ctx, uint64_t id, uint8_t ep, uint8_t *dst,
                  size_t len, uint32_t timeout) {
    return bulk(ctx, id, ep, dst, nullptr, len, timeout, true);
}
int32_t bulk_write(void *ctx, uint64_t id, uint8_t ep, const uint8_t *src,
                   size_t len, uint32_t timeout) {
    return bulk(ctx, id, ep, nullptr, src, len, timeout, false);
}

bool quiesce(void *) {
    if (inFlight || fault || claimed(0)) return false;
    for (const auto &c : claims) if (c.token) return false;
    /* A device close, client deregistration, USB library uninstall or VBUS
     * release failure leaves the ELF mapped for retry/recovery. */
    for (auto &d : devices) if (d.handle) {
        if (usb_host_device_close(client, d.handle) != ESP_OK) return false;
        d = {};
    }
    if (transfer) {
        if (usb_host_transfer_free(transfer) != ESP_OK) return false;
        transfer = nullptr;
    }
    if (client) {
        if (usb_host_client_deregister(client) != ESP_OK) return false;
        client = nullptr;
    }
    if (installed) {
        esp_err_t rc = usb_host_device_free_all();
        if (rc != ESP_OK && rc != ESP_ERR_NOT_FINISHED) return false;
        bool freed = rc == ESP_OK;
        for (uint32_t i = 0; !freed && i < kTeardownTicks; ++i) {
            uint32_t flags = 0;
            rc = usb_host_lib_handle_events(1, &flags);
            if (rc != ESP_OK && rc != ESP_ERR_TIMEOUT) return false;
            if (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) freed = true;
        }
        if (!freed || usb_host_uninstall() != ESP_OK) return false;
        installed = false;
    }
    if (powerLease) {
        if (!power || !power->release_host(power->context, powerLease)) return false;
        powerLease = 0;
    }
    running = false;
    return !power || power->quiesce(power->context);
}
void stop() {
    if (!quiesce(nullptr)) return;
    power = nullptr;
    queueHead = queueTail = queueCount = 0;
    fault = false;
}
bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (running || installed || client || transfer || powerLease || fault ||
        !deps || count != 1 || !equals(deps[0].capability_id, "board.power.vbus") ||
        deps[0].api_version != RISC_USB_VBUS_API_V1 || !deps[0].api) {
        std::printf("USBCTRL start-failed stage=dependency-or-state rc=0\n");
        return false;
    }
    const auto *api = static_cast<const risc_usb_vbus_api_v1 *>(deps[0].api);
    if (api->api_version != RISC_USB_VBUS_API_V1 ||
        api->struct_size < sizeof(*api) || !api->acquire_host ||
        !api->release_host || !api->quiesce) {
        std::printf("USBCTRL start-failed stage=vbus-abi rc=0\n");
        return false;
    }
    power = api;
    /* The board provider alone decides whether host sourcing is electrically
     * legal. The controller never touches charger/I2C registers in firmware. */
    if (!power->acquire_host(power->context, 500, &powerLease) || !powerLease) {
        std::printf("USBCTRL start-failed stage=vbus-acquire rc=0 lease=%u\n",
                    powerLease ? 1u : 0u);
        // Acquire may fail after partially changing the board state. A lease
        // must be released by its owner before the ELF can be reused/unmapped.
        if (powerLease && !quiesce(nullptr))
            std::printf("USBCTRL cleanup-failed stage=vbus-acquire\n");
        else if (!powerLease) power = nullptr;
        return false;
    }
    std::printf("USBCTRL stage=vbus-acquired\n");
    usb_host_config_t config = {};
    config.skip_phy_setup = false;
    config.intr_flags = ESP_INTR_FLAG_LEVEL1;
    const esp_err_t install_rc = usb_host_install(&config);
    if (install_rc != ESP_OK) {
        std::printf("USBCTRL start-failed stage=usb-host-install rc=%d\n",
                    static_cast<int>(install_rc));
        if (!quiesce(nullptr)) std::printf("USBCTRL cleanup-failed stage=usb-host-install\n");
        return false;
    }
    installed = true;
    std::printf("USBCTRL stage=usb-host-installed\n");
    usb_host_client_config_t registration = {};
    registration.is_synchronous = false;
    registration.max_num_event_msg = kEvents;
    registration.async.client_event_callback = client_event;
    registration.async.callback_arg = nullptr;
    const esp_err_t register_rc = usb_host_client_register(&registration, &client);
    if (register_rc != ESP_OK) {
        std::printf("USBCTRL start-failed stage=client-register rc=%d\n",
                    static_cast<int>(register_rc));
        if (!quiesce(nullptr)) std::printf("USBCTRL cleanup-failed stage=client-register\n");
        return false;
    }
    std::printf("USBCTRL stage=client-registered\n");
    const esp_err_t alloc_rc = usb_host_transfer_alloc(kBuffer, 0, &transfer);
    if (alloc_rc != ESP_OK) {
        std::printf("USBCTRL start-failed stage=transfer-alloc rc=%d\n",
                    static_cast<int>(alloc_rc));
        if (!quiesce(nullptr)) std::printf("USBCTRL cleanup-failed stage=transfer-alloc\n");
        return false;
    }
    running = true;
    std::printf("USBCTRL stage=controller-running\n");
    return true;
}
static const risc_usb_controller_api_v1 interface = {
    RISC_USB_CONTROLLER_API_V1, sizeof(risc_usb_controller_api_v1), nullptr,
    next_event, configuration, claim_interface, release_interface,
    control, bulk_read, bulk_write, quiesce
};
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "usb-controller-esp32s3", "usb.controller", RISC_USB_CONTROLLER_API_V1,
    &interface, start, stop, []() -> bool { return quiesce(nullptr); }
};
} // namespace
extern "C" __attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : nullptr;
}
