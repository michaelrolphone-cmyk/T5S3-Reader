/* HID-capable ESP32-S3 controller. Original physical hardware ownership,
 * VBUS, DMA, callbacks, control, bulk and recovery remain in driver_base.cpp.
 * This file extends the SAME ELF rather than introducing a firmware bridge. */
#include "RiscUsbHidV1.h"
#include "RiscUsbInterruptV1.h"
#define t5_driver_get t5_usb_controller_base_get
#include "driver_base.cpp"
#undef t5_driver_get

namespace {
bool interrupt_mps(Device *d, const Claim *c, uint8_t endpoint,
                   uint16_t *out_packet) {
    const usb_config_desc_t *config = nullptr;
    if (!d || !c || !d->attached || !out_packet ||
        !(endpoint & 0x80u) || (endpoint & 0x70u) ||
        usb_host_get_active_config_descriptor(d->handle, &config) != ESP_OK ||
        !config || config->wTotalLength < 9 ||
        config->wTotalLength > RISC_USB_CONFIG_LIMIT) return false;
    const auto *bytes = reinterpret_cast<const uint8_t *>(config);
    const size_t length = config->wTotalLength;
    bool selected = false, found = false;
    for (size_t pos = 0; pos < length;) {
        if (length - pos < 2) return false;
        const uint8_t n = bytes[pos], type = bytes[pos + 1];
        if (n < 2 || n > length - pos) return false;
        if (type == 4) {
            if (selected) return found;
            if (n < 6) return false;
            selected = n >= 9 && bytes[pos + 2] == c->number &&
                       bytes[pos + 3] == c->alternate;
        } else if (selected && type == 5) {
            if (n < 7) return false;
            if (bytes[pos + 2] == endpoint &&
                (bytes[pos + 3] & 3u) == 3u) {
                const uint16_t packet = static_cast<uint16_t>(bytes[pos + 4] |
                                                               (bytes[pos + 5] << 8));
                if (found || !packet || packet > RISC_USB_HID_MAX_REPORT)
                    return false;
                *out_packet = packet;
                found = true;
            }
        }
        pos += n;
    }
    return found;
}
int32_t interrupt_read(void *, uint64_t id, uint8_t endpoint,
                       uint8_t *dst, size_t capacity, uint32_t timeout) {
    Claim *c = claim(id);
    Device *d = c ? device(c->physical_device) : nullptr;
    uint16_t packet = 0;
    if (!running || !d || !d->attached || !dst || !timeout || timeout > 100 ||
        !interrupt_mps(d, c, endpoint, &packet) || capacity < packet ||
        capacity > RISC_USB_HID_MAX_REPORT || !idle_transfer()) return -1;
    transfer->device_handle = d->handle;
    transfer->bEndpointAddress = endpoint;
    /* ESP-IDF requires whole endpoint-MPS transfer sizes for interrupt IN. */
    transfer->num_bytes = packet;
    transfer->callback = complete_transfer;
    transfer->context = nullptr;
    completed = false;
    inFlight = true;
    if (usb_host_transfer_submit(transfer) != ESP_OK) {
        inFlight = false;
        return -1;
    }
    if (!wait_completion(timeout)) {
        /* Empty is distinct from failure, and legal only after IDF callback
         * completion and endpoint drain. The base timeout path halts, flushes
         * and clears a pending non-control transfer. */
        return !inFlight && !completed && !fault ? 0 : -1;
    }
    const int32_t n = transfer->actual_num_bytes;
    if (n < 0 || static_cast<size_t>(n) > capacity || n > packet) return -1;
    if (n) std::memcpy(dst, transfer->data_buffer, static_cast<size_t>(n));
    return n;
}
bool release_with_interrupt(void *context, uint64_t id) {
    /* The legacy release checks only bulk endpoint membership. An in-flight
     * HID read must be drained against its own claimed interrupt endpoint
     * before that release, rather than leaving callbacks targeting freed ELF. */
    Claim *c = nullptr;
    for (auto &candidate : claims)
        if (id && candidate.token == id) { c = &candidate; break; }
    Device *d = nullptr;
    if (c) for (auto &candidate : devices)
        if (candidate.handle && candidate.token == c->physical_device) {
            d = &candidate; break;
        }
    if (!running || !c || !d) return false;
    if (inFlight && transfer && transfer->device_handle == d->handle &&
        transfer->bEndpointAddress) {
        uint16_t packet = 0;
        if (interrupt_mps(d, c, transfer->bEndpointAddress, &packet)) {
            if (!drain_bulk(false)) return false;
        }
    }
    return release_interface(context, id);
}
static const risc_usb_controller_interrupt_v1 hid_interface = {
    {RISC_USB_CONTROLLER_API_V1, sizeof(risc_usb_controller_interrupt_v1), nullptr,
     next_event, configuration, claim_interface, release_with_interrupt,
     control, bulk_read, bulk_write, quiesce},
    interrupt_read
};
static const risc_driver_diagnostics_v2 hid_driver = {{
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_diagnostics_v2),
    "usb-controller-esp32s3", "usb.controller", RISC_USB_CONTROLLER_API_V1,
    &hid_interface.controller, start, stop,
    []() -> bool { return quiesce(nullptr); }
}, startup_error};
} // namespace
extern "C" __attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &hid_driver.base : nullptr;
}
