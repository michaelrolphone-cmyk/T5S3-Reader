/* HID-capable ESP32-S3 controller. Keep the existing physical USB/VBUS/event,
 * bulk, control, DMA and teardown implementation in driver_base.cpp; compile
 * additional claimed interrupt transfers in this SAME hardware-owning ELF. */
#include "RiscUsbHidV1.h"
#include "RiscUsbInterruptV1.h"
#define t5_driver_get t5_usb_controller_base_get
#include "driver_base.cpp"
#undef t5_driver_get

namespace {
/* Verify physical descriptor ownership independent of the usb.host claim
 * check. Never read another interface's endpoint on a composite device. */
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
            if (n < 9) return false;
            selected = bytes[pos + 2] == c->number &&
                       bytes[pos + 3] == c->alternate;
        } else if (selected && type == 5) {
            if (n < 7) return false;
            if (bytes[pos + 2] == endpoint &&
                (bytes[pos + 3] & 3u) == 3u) {
                const uint16_t packet = static_cast<uint16_t>(bytes[pos + 4] |
                                                               (bytes[pos + 5] << 8));
                /* IDF requires a complete endpoint MPS for interrupt IN. */
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
        /* A no-input timeout returns zero ONLY after the existing controller
         * flushes the endpoint and drains its callback. A failed drain or
         * transfer status must not be disguised as an empty report. */
        return !inFlight && !completed && !fault ? 0 : -1;
    }
    const int32_t n = transfer->actual_num_bytes;
    if (n < 0 || static_cast<size_t>(n) > capacity || n > packet) return -1;
    if (n) std::memcpy(dst, transfer->data_buffer, static_cast<size_t>(n));
    return n;
}
/* Explicitly set the extended struct_size; the legacy table reports its own
 * smaller size and would otherwise fail closed at usb.host startup. */
static const risc_usb_controller_interrupt_v1 hid_interface = {
    {RISC_USB_CONTROLLER_API_V1, sizeof(risc_usb_controller_interrupt_v1), nullptr,
     next_event, configuration, claim_interface, release_interface,
     control, bulk_read, bulk_write, quiesce},
    interrupt_read
};
static const risc_driver_v2 hid_driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "usb-controller-esp32s3", "usb.controller", RISC_USB_CONTROLLER_API_V1,
    &hid_interface.controller, start, stop,
    []() -> bool { return quiesce(nullptr); }
};
} // namespace
extern "C" __attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &hid_driver : nullptr;
}
