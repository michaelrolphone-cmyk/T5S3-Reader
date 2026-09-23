#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
using TickType_t = uint32_t;
using esp_err_t = int;
using usb_device_handle_t = void *;
constexpr int ESP_OK = 0, USB_TRANSFER_STATUS_COMPLETED = 0;
constexpr int USB_TRANSFER_STATUS_STALL = 2;
constexpr unsigned RISC_USB_HOST_MAX_CLAIMS = 4, kTeardownTicks = 8;
#define pdMS_TO_TICKS(x) (x)
struct usb_transfer_t {
    void *device_handle, *context;
    uint8_t bEndpointAddress;
    int num_bytes, actual_num_bytes, status;
    void (*callback)(usb_transfer_t *);
    uint8_t data_buffer[64];
};
static unsigned ticks, submits, halts, frees, clears;
static usb_transfer_t *queued[4];
static bool deliver, cancel, stuck, stall;
static bool reject_submit;
static int packet_byte = -1;
TickType_t xTaskGetTickCount() { return ticks; }
int usb_host_transfer_alloc(unsigned n, int, usb_transfer_t **out) {
    assert(n <= 64); *out = new usb_transfer_t{}; return 0;
}
int usb_host_transfer_submit(usb_transfer_t *t) {
    ++submits;
    if (reject_submit) return -1;
    for (auto &q : queued) if (!q) { q = t; return 0; }
    assert(false); return -1;
}
int usb_host_transfer_free(usb_transfer_t *t) {
    for (auto *q : queued) assert(q != t);
    ++frees; delete t; return 0;
}
int usb_host_endpoint_halt(void *, uint8_t) { ++halts; return 0; }
int usb_host_endpoint_flush(void *, uint8_t) { cancel = true; return 0; }
int usb_host_endpoint_clear(void *, uint8_t) { ++clears; return 0; }
bool pump(unsigned wait_ticks) {
    ticks += wait_ticks;
    if ((deliver || cancel) && !stuck) {
        for (auto &q : queued) if (q) {
            q->status = cancel ? 1 : stall ? USB_TRANSFER_STATUS_STALL : 0;
            q->actual_num_bytes = 1;
            q->data_buffer[0] = packet_byte < 0 ? q->bEndpointAddress : packet_byte;
            auto *done = q; q = nullptr; done->callback(done);
        }
        cancel = false;
    }
    return true;
}
#include "../../Drivers/usb_controller_esp32s3/PendingInterrupt.h"
int main() {
    uint8_t out[64]{};
    auto handle = reinterpret_cast<void *>(1);
    for (int i = 0; i < 50; ++i)
        assert(read_interrupt(1, handle, 0x81, 8, out, 10) == 0);
    assert(submits == 1 && halts == 0 && frees == 0);
    assert(ticks == 0); // Empty polling never spends the requested wait budget.
    // An idle keyboard must not block a gamepad's separate endpoint DMA.
    assert(read_interrupt(2, handle, 0x82, 8, out, 10) == 0);
    assert(submits == 2);
    deliver = true;
    assert(read_interrupt(2, handle, 0x82, 8, out, 10) == 1 && out[0] == 0x82);
    assert(read_interrupt(1, handle, 0x81, 8, out, 10) == 1 && out[0] == 0x81);
    assert(submits == 4 && halts == 0);
    assert(ticks == 0); // Draining ready reports does not wait for future ones.
    assert(interrupts[0].pending && interrupts[1].pending);
    // A release can arrive during the caller's idle interval, before it calls
    // read again. Do not require another read just to arm that USB request.
    packet_byte = 0; // Button release received before the next class poll.
    assert(pump(1));
    assert(interrupts[0].ready && interrupts[1].ready);
    assert(read_interrupt(1, handle, 0x81, 8, out, 10) == 1 && out[0] == 0);
    packet_byte = -1;
    stall = true;
    assert(read_interrupt(1, handle, 0x81, 8, out, 10) == -1 && clears == 1);
    stall = false;
    assert(read_interrupt(1, handle, 0x81, 8, out, 10) == 1 && clears == 1);
    reject_submit = true;
    assert(read_interrupt(1, handle, 0x81, 8, out, 10) == -1);
    assert(!interrupts[0].pending);
    reject_submit = false;
    assert(read_interrupt(1, handle, 0x81, 8, out, 10) == 1 && interrupts[0].pending);
    deliver = false;
    assert(read_interrupt(1, handle, 0x81, 8, out, 10) == 0);
    stuck = true;
    assert(!drain_interrupt(interrupts[0]));
    assert(frees == 0 && interrupts[0].pending); // Retain live DMA on failure.
    stuck = false;
    assert(drain_interrupt(interrupts[0]));
    assert(drain_interrupt(interrupts[1]));
    assert(frees == 2);
    puts("Controller persistent interrupt reads, independent endpoints and safe drain: PASS");
}
