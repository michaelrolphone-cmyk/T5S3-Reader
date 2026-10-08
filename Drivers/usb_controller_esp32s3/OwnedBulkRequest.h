#pragma once
#include "BoundedIdf.h"
#include "RiscStreamResultV1.h"
#include <cstdint>
#include <cstring>

namespace RiscUsbController {
/* One preallocated, controller-owned DMA request. The owner calls step once
 * per cooperative poll; each step executes at most three try-only SDK calls
 * and one callback. It never sleeps, runs role/lifecycle work, or keeps an app
 * pointer. Take is the only copy-out path. RETAINED is terminal and sticky,
 * even when a late callback subsequently returns DMA custody.
 *
 * Port: monotonic now(), library_idle(), resolve(), submit(), client_step(),
 * cancel_step(), clear(). The IDF adaptation supplies those exact operations.
 * Request deadlines include admission, submission, pumping and cancellation.
 */
class OwnedBulkRequest {
public:
    enum class State : uint8_t { Idle, Resolve, Submit, Active, Cancel, Drain,
                                  Clear, Done, Retained };
    static constexpr size_t capacity = 4096;
    State state() const { return phase; }
    bool owns_storage() const { return phase != State::Idle; }
    bool retained() const { return phase == State::Retained; }
    uint64_t claim_id() const { return ownerClaim; }
    uint64_t deadline() const { return until; }
    bool callback_seen() const { return callbackSeen; }

    template<class Port>
    int32_t begin(Port &port, usb_transfer_t *owned, usb_device_handle_t device,
                  uint64_t claim, uint8_t ep, const uint8_t *source,
                  size_t length, uint32_t budget)
    {
        if (phase != State::Idle) return retained() ? RISC_STREAM_RETAINED : RISC_STREAM_BUSY;
        if (!owned || !owned->data_buffer || owned->data_buffer_size < capacity ||
            owned->num_isoc_packets != 0 ||
            !device || !claim || !(ep & 0x0fu) || (ep & 0x70u) || !length ||
            length > capacity || !budget || budget > 1000 ||
            (!(ep & 0x80u) && !source)) return RISC_STREAM_INVALID;
        const uint64_t now = port.now();
        if (UINT64_MAX - now < budget) return RISC_STREAM_IO;
        dma = owned; ownerClaim = claim; requested = length;
        reading = (ep & 0x80u) != 0;
        lastNow = now; until = now + budget;
        /* Reserve part of the SAME caller budget to prove cancellation.
         * No second drain timeout is started. Small budgets may retain. */
        const uint32_t reserve = budget / 4 < 25 ? budget / 4 : 25;
        cancelAt = until - reserve;
        callbackSeen = submitted = cancelIssued = false;
        endpoint = nullptr; reason = RISC_STREAM_OK; result = RISC_STREAM_AGAIN;
        dma->device_handle = device; dma->bEndpointAddress = ep;
        dma->num_bytes = static_cast<int>(length);
        dma->flags = 0; dma->callback = callback; dma->context = this;
        if (!reading && source) std::memcpy(dma->data_buffer, source, length);
        phase = State::Resolve;
        if (!within(port)) return finish_or_retain(RISC_STREAM_TIMEOUT);
        return RISC_STREAM_AGAIN;
    }

    void cancel()
    {
        if (phase != State::Idle && phase != State::Done && phase != State::Retained &&
            reason == RISC_STREAM_OK) reason = RISC_STREAM_CANCELLED;
    }

    template<class Port> int32_t step(Port &port)
    {
        if (phase == State::Idle) return RISC_STREAM_CLOSED;
        if (phase == State::Retained) return RISC_STREAM_RETAINED;
        if (phase == State::Done) return result;
        const uint64_t now = port.now();
        if (now < lastNow) return finish_or_retain(RISC_STREAM_IO);
        lastNow = now;
        if (now >= until) return finish_or_retain(
            reason == RISC_STREAM_OK ? RISC_STREAM_TIMEOUT : reason);
        if (reason == RISC_STREAM_OK && now >= cancelAt) reason = RISC_STREAM_TIMEOUT;
        if (reason != RISC_STREAM_OK && !submitted) return finish(reason);
        if (reason != RISC_STREAM_OK && phase == State::Active) phase = State::Cancel;

        esp_err_t rc;
        switch (phase) {
        case State::Resolve: {
            rc = port.library_idle();
            if (rc == ESP_ERR_NOT_FINISHED) break;
            if (rc != ESP_OK) return finish(RISC_STREAM_IO);
            if (!within(port)) return finish_or_retain(RISC_STREAM_TIMEOUT);
            uint16_t packet = 0;
            rc = port.resolve(dma->device_handle, dma->bEndpointAddress, &endpoint, &packet);
            if (rc == ESP_ERR_NOT_FINISHED) break;
            if (rc != ESP_OK || !endpoint || !packet || packet > 512)
                return finish(RISC_STREAM_IO);
            size_t count = reading ? ((requested + packet - 1) / packet) * packet : requested;
            if (count > capacity) return finish(RISC_STREAM_LIMIT);
            dma->num_bytes = static_cast<int>(count);
            phase = State::Submit;
            break;
        }
        case State::Submit:
            rc = port.library_idle();
            if (rc == ESP_ERR_NOT_FINISHED) break;
            if (rc != ESP_OK) return finish(RISC_STREAM_IO);
            if (!within(port)) return finish_or_retain(RISC_STREAM_TIMEOUT);
            /* Submission cannot invoke the controller callback inline. */
            rc = port.submit(endpoint, dma);
            if (rc == ESP_ERR_NOT_FINISHED) break;
            if (rc != ESP_OK) return finish(RISC_STREAM_IO);
            submitted = true; phase = State::Active;
            break;
        case State::Active: {
            bool work = false;
            rc = port.client_step(endpoint, &work);
            if (retained()) return RISC_STREAM_RETAINED;
            if (rc != ESP_OK && rc != ESP_ERR_NOT_FINISHED) return retain();
            if (!within(port)) return finish_or_retain(RISC_STREAM_TIMEOUT);
            const esp_err_t idle = port.library_idle();
            if (!within(port)) return finish_or_retain(RISC_STREAM_TIMEOUT);
            if (idle != ESP_OK && idle != ESP_ERR_NOT_FINISHED) return retain();
            if (callbackSeen) {
                if (status == USB_TRANSFER_STATUS_COMPLETED && idle == ESP_OK) {
                    if (actual < 0 || static_cast<size_t>(actual) > requested)
                        return finish(RISC_STREAM_IO);
                    return finish(actual);
                }
                reason = status == USB_TRANSFER_STATUS_NO_DEVICE ?
                    RISC_STREAM_DISCONNECTED : RISC_STREAM_IO;
                phase = State::Cancel;
            }
            break;
        }
        case State::Cancel:
            /* A normal completion that won the cancellation race needs no
             * halt. A started halt must still be acknowledged and cleared. */
            if (callbackSeen && !cancelIssued && status == USB_TRANSFER_STATUS_COMPLETED)
                return finish(reason);
            cancelIssued = true;
            rc = port.cancel_step(endpoint);
            if (rc == ESP_ERR_NOT_FINISHED) break;
            if (rc != ESP_OK) return retain();
            phase = State::Drain;
            break;
        case State::Drain: {
            bool work = false;
            rc = port.client_step(endpoint, &work);
            if (retained()) return RISC_STREAM_RETAINED;
            if (rc != ESP_OK && rc != ESP_ERR_NOT_FINISHED) return retain();
            if (callbackSeen) phase = State::Clear;
            break;
        }
        case State::Clear:
            rc = port.clear(endpoint);
            if (!within(port)) return retain();
            if (rc == ESP_ERR_NOT_FINISHED) break;
            if (rc != ESP_OK) return retain();
            /* Cancel+callback+clear are all necessary; callback alone does
             * not release an unfinished HCD command or a halted endpoint. */
            cancelIssued = false;
            return finish(reason);
        default: return retain();
        }
        /* Contract-fault detection is additional to genuinely try-only lower
         * calls. It does not turn a blocking callback into a bounded one. */
        if (!within(port)) return finish_or_retain(RISC_STREAM_TIMEOUT);
        return RISC_STREAM_AGAIN;
    }

    int32_t take(uint64_t claim, uint8_t *destination, size_t size)
    {
        if (claim != ownerClaim || !claim) return RISC_STREAM_INVALID;
        if (phase == State::Retained) return RISC_STREAM_RETAINED;
        if (phase != State::Done) return RISC_STREAM_AGAIN;
        if (reading && result > 0) {
            if (!destination || size < static_cast<size_t>(result)) return RISC_STREAM_LIMIT;
            std::memcpy(destination, dma->data_buffer, static_cast<size_t>(result));
        }
        const int32_t answer = result;
        dma->callback = nullptr; dma->context = nullptr;
        phase = State::Idle; dma = nullptr; endpoint = nullptr; ownerClaim = 0;
        return answer;
    }
private:
    State phase = State::Idle;
    usb_transfer_t *dma = nullptr;
    void *endpoint = nullptr;
    uint64_t ownerClaim = 0, lastNow = 0, until = 0, cancelAt = 0;
    size_t requested = 0;
    bool reading = false, callbackSeen = false, submitted = false, cancelIssued = false;
    int actual = 0;
    usb_transfer_status_t status = USB_TRANSFER_STATUS_ERROR;
    int32_t reason = RISC_STREAM_OK, result = RISC_STREAM_AGAIN;
    static void callback(usb_transfer_t *transfer)
    {
        auto *self = static_cast<OwnedBulkRequest *>(transfer->context);
        if (!self || self->dma != transfer || !self->submitted || self->callbackSeen) {
            if (self) self->retain();
            return;
        }
        self->callbackSeen = true;
        self->actual = transfer->actual_num_bytes;
        self->status = transfer->status;
        /* Never publish, retry, free, or thaw a retained request here. */
    }
    template<class Port> bool within(Port &port)
    {
        const uint64_t now = port.now();
        if (now < lastNow) { retain(); return false; }
        lastNow = now;
        return now < until;
    }
    int32_t retain() { phase = State::Retained; result = RISC_STREAM_RETAINED; return result; }
    int32_t finish(int32_t value) { phase = State::Done; result = value; return value; }
    int32_t finish_or_retain(int32_t value)
    {
        if (phase == State::Retained || (submitted &&
            (!callbackSeen || cancelIssued || status != USB_TRANSFER_STATUS_COMPLETED)))
            return retain();
        return finish(value);
    }
};
} // namespace RiscUsbController
