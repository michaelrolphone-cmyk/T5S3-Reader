#pragma once
#include "BoundedControl.h"
#include "RiscStreamResultV1.h"
#include <cstdint>
#include <cstring>

namespace RiscUsbController {
/* One copied control setup/payload and one exact operation identity. The
 * controller's existing DMA buffer owns all bytes until terminal take(). */
class OwnedControlRequest {
public:
    enum class State : uint8_t { Idle, Submit, Active, Cancel, Drain, Done, Retained };
    State state() const { return phase; }
    bool owns_storage() const { return phase != State::Idle; }
    bool retained() const { return phase == State::Retained; }
    uint64_t token() const { return request; }
    bool callback_seen() const { return callbackSeen; }
    template<class Port>
    int32_t begin(Port &port, usb_transfer_t *owned, usb_device_handle_t device,
        uint64_t identity, uint8_t type, uint8_t command, uint16_t value,
        uint16_t index, const uint8_t *outgoing, uint16_t length, uint32_t budget)
    {
        if (owns_storage()) return retained() ? RISC_STREAM_RETAINED : RISC_STREAM_BUSY;
        if (!owned || !owned->data_buffer || owned->data_buffer_size < 4104 || owned->num_isoc_packets ||
            !device || !identity || !budget || budget > 1000 || length > 4096 ||
            (!(type & 0x80u) && length && !outgoing)) return RISC_STREAM_INVALID;
        const uint64_t now = port.now();
        if (UINT64_MAX - now < budget) return RISC_STREAM_IO;
        request = identity; dma = owned; requested = length; reading = (type & 0x80u) != 0;
        lastNow = now; until = now + budget;
        const uint32_t reserve = budget / 4 < 25 ? budget / 4 : 25;
        cancelAt = until - reserve;
        submitted = callbackSeen = false; reason = RISC_STREAM_OK; result = RISC_STREAM_AGAIN;
        uint8_t *b = dma->data_buffer;
        b[0] = type; b[1] = command; b[2] = static_cast<uint8_t>(value); b[3] = value >> 8;
        b[4] = static_cast<uint8_t>(index); b[5] = index >> 8;
        b[6] = static_cast<uint8_t>(length); b[7] = length >> 8;
        if (!reading && length && outgoing) std::memcpy(b + 8, outgoing, length);
        dma->device_handle = device; dma->bEndpointAddress = 0;
        dma->num_bytes = length + 8; dma->flags = 0;
        dma->callback = complete; dma->context = this;
        phase = State::Submit;
        if (!within(port)) return expire(RISC_STREAM_TIMEOUT);
        return RISC_STREAM_AGAIN;
    }
    void cancel(uint64_t identity)
    {
        if (identity == request && phase != State::Idle && phase != State::Done &&
            phase != State::Retained && reason == RISC_STREAM_OK) reason = RISC_STREAM_CANCELLED;
    }
    template<class Port> int32_t step(Port &port, uint64_t identity)
    {
        if (!identity || identity != request) return RISC_STREAM_INVALID;
        if (phase == State::Retained) return RISC_STREAM_RETAINED;
        if (risc_usb_control_faulted()) return retain();
        if (phase == State::Done) return result;
        if (phase == State::Idle) return RISC_STREAM_CLOSED;
        if (risc_usb_control_faulted()) return retain();
        const uint64_t now = port.now();
        if (now < lastNow) return expire(RISC_STREAM_IO);
        lastNow = now;
        if (now >= until) return expire(reason == RISC_STREAM_OK ? RISC_STREAM_TIMEOUT : reason);
        if (reason == RISC_STREAM_OK && now >= cancelAt) reason = RISC_STREAM_TIMEOUT;
        if (reason != RISC_STREAM_OK && !submitted) return done(reason);
        if (reason != RISC_STREAM_OK && phase == State::Active) phase = State::Cancel;
        esp_err_t rc;
        switch (phase) {
        case State::Submit:
            rc = port.submit(dma, request);
            if (rc == ESP_OK) { submitted = true; phase = State::Active; }
            else if (rc != ESP_ERR_NOT_FINISHED) return done(RISC_STREAM_IO);
            break;
        case State::Active:
        case State::Drain:
            rc = port.poll(request);
            if (retained() || risc_usb_control_faulted()) return retain();
            if (!within(port)) return expire(RISC_STREAM_TIMEOUT);
            if (rc == ESP_ERR_NOT_FINISHED) break;
            if (rc != ESP_OK || !callbackSeen) return retain();
            if (reason != RISC_STREAM_OK) return done(reason);
            if (status == USB_TRANSFER_STATUS_NO_DEVICE) return done(RISC_STREAM_DISCONNECTED);
            if (status != USB_TRANSFER_STATUS_COMPLETED) return done(RISC_STREAM_IO);
            if (actual < 8 || static_cast<unsigned>(actual - 8) > requested) return done(RISC_STREAM_IO);
            return done(actual - 8);
        case State::Cancel:
            rc = port.cancel(request);
            if (rc == ESP_OK) phase = State::Drain;
            else if (rc != ESP_ERR_NOT_FINISHED) return retain();
            break;
        default: return retain();
        }
        if (!within(port)) return expire(RISC_STREAM_TIMEOUT);
        return RISC_STREAM_AGAIN;
    }
    int32_t take(uint64_t identity, uint8_t *destination, size_t capacity)
    {
        if (!identity || identity != request) return RISC_STREAM_INVALID;
        if (retained()) return RISC_STREAM_RETAINED;
        if (risc_usb_control_faulted()) return retain();
        if (phase != State::Done) return RISC_STREAM_AGAIN;
        if (reading && result > 0) {
            if (!destination || capacity < static_cast<size_t>(result)) return RISC_STREAM_LIMIT;
            std::memcpy(destination, dma->data_buffer + 8, static_cast<size_t>(result));
        }
        const int32_t answer = result;
        dma->callback = nullptr; dma->context = nullptr;
        phase = State::Idle; request = 0; dma = nullptr;
        return answer;
    }
private:
    State phase = State::Idle;
    usb_transfer_t *dma = nullptr;
    uint64_t request = 0, lastNow = 0, until = 0, cancelAt = 0;
    uint16_t requested = 0;
    bool reading = false, submitted = false, callbackSeen = false;
    int32_t result = RISC_STREAM_AGAIN, reason = RISC_STREAM_OK;
    int actual = 0;
    usb_transfer_status_t status = USB_TRANSFER_STATUS_ERROR;
    static void complete(usb_transfer_t *transfer)
    {
        auto *self = static_cast<OwnedControlRequest *>(transfer->context);
        if (!self || self->dma != transfer || !self->submitted || self->callbackSeen) {
            if (self) self->retain();
            return;
        }
        self->callbackSeen = true; self->actual = transfer->actual_num_bytes;
        self->status = transfer->status;
    }
    template<class Port> bool within(Port &port)
    {
        if (risc_usb_control_faulted()) { retain(); return false; }
        uint64_t now = port.now();
        if (now < lastNow) { retain(); return false; }
        lastNow = now; return now < until;
    }
    int32_t retain() {
        risc_usb_control_retain(request); phase = State::Retained;
        result = RISC_STREAM_RETAINED; return result;
    }
    int32_t done(int32_t value) { phase = State::Done; result = value; return value; }
    int32_t expire(int32_t value) {
        if (retained() || (submitted && !callbackSeen)) return retain();
        return done(value);
    }
};
} // namespace RiscUsbController
