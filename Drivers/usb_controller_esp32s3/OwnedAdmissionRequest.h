#pragma once
#include "BoundedAdmission.h"
#include "RiscStreamResultV1.h"
#include <cstdint>
#include <cstring>

namespace RiscUsbController {
/* One owner-poll operation; all output stays here until take(). Preparation
 * of the finite SDK pool is separate and is never called by this machine. */
class OwnedAdmissionRequest {
public:
    enum class Kind : uint8_t { Configuration, Claim, Release, Close };
    enum class State : uint8_t { Idle, Begin, Work, Rollback, Closing, Done, Retained };
    bool owns_storage() const { return phase != State::Idle; }
    bool retained() const { return phase == State::Retained; }
    State state() const { return phase; }
    Kind kind() const { return operation; }
    uint64_t token() const { return identity; }
    bool interface_released() const { return released; }
    bool reference_closed() const { return closed; }
    bool cleanup_deferred() const { return deferred; }

    template<class Clock>
    int32_t begin(Clock &clock, Kind kind, usb_host_client_handle_t client,
                  usb_device_handle_t device, uint64_t token, uint8_t number,
                  uint8_t alternate, bool close_after_release, uint32_t budget)
    {
        if (owns_storage()) return retained() ? RISC_STREAM_RETAINED : RISC_STREAM_BUSY;
        if (!client || !device || !token || !budget || budget > 1000) return RISC_STREAM_INVALID;
        uint64_t now = clock.now();
        if (UINT64_MAX - now < budget) return RISC_STREAM_IO;
        operation = kind; owner = client; dev = device; identity = token;
        iface = number; alt = alternate; closeAfter = close_after_release;
        began = false; released = closed = deferred = false;
        lastNow = now; until = now + budget;
        const uint32_t reserve = budget / 4 < 25 ? budget / 4 : 25;
        rollbackAt = until - reserve;
        result = RISC_STREAM_AGAIN; reason = RISC_STREAM_OK; configLength = 0;
        phase = kind == Kind::Close ? State::Closing : State::Begin;
        return RISC_STREAM_AGAIN;
    }

    void cancel()
    {
        if (phase != State::Idle && phase != State::Done && phase != State::Retained &&
            reason == RISC_STREAM_OK) reason = RISC_STREAM_CANCELLED;
    }

    template<class Clock> int32_t step(Clock &clock)
    {
        if (phase == State::Idle) return RISC_STREAM_CLOSED;
        if (phase == State::Retained) return RISC_STREAM_RETAINED;
        if (phase == State::Done) return result;
        const uint64_t now = clock.now();
        if (now < lastNow) return expire(RISC_STREAM_IO);
        lastNow = now;
        if (risc_usb_admission_faulted()) return retain();
        if (now >= until) return expire(reason == RISC_STREAM_OK ? RISC_STREAM_TIMEOUT : reason);
        if (operation == Kind::Claim && reason == RISC_STREAM_OK && now >= rollbackAt)
            reason = RISC_STREAM_TIMEOUT;
        if (reason != RISC_STREAM_OK) {
            if (operation == Kind::Claim) {
                if (!began) return done(reason);
                phase = State::Rollback;
            } else if (operation == Kind::Configuration || (!began && !released && !closed)) {
                return done(reason);
            }
            /* Once release/close starts, complete its admitted cleanup rather
             * than pretending cancellation restores already released state. */
        }
        esp_err_t rc = ESP_ERR_INVALID_STATE;
        switch (phase) {
        case State::Begin:
            if (operation == Kind::Configuration) {
                rc = risc_usb_configuration_copy(owner, dev, configuration, sizeof(configuration),
                                                  &configLength, &vid, &pid);
                if (!check(clock)) return expire(RISC_STREAM_TIMEOUT);
                if (rc == ESP_OK) return done(RISC_STREAM_OK);
                if (rc != ESP_ERR_NOT_FINISHED) return done(map(rc));
            } else if (operation == Kind::Claim) {
                rc = risc_usb_claim_begin(owner, dev, iface, alt, identity);
                if (rc == ESP_OK) { began = true; phase = State::Work; }
                else if (rc != ESP_ERR_NOT_FINISHED) return done(map(rc));
            } else {
                risc_usb_claim_state state{};
                if (!risc_usb_claim_state_copy(identity, &state)) return done(RISC_STREAM_CLOSED);
                if (state.retained) return retain();
                began = true; phase = State::Work;
            }
            break;
        case State::Work:
            if (operation == Kind::Claim) {
                rc = risc_usb_claim_step(identity);
                if (!check(clock)) return expire(RISC_STREAM_TIMEOUT);
                if (rc == ESP_OK) return done(RISC_STREAM_OK);
                if (rc != ESP_ERR_NOT_FINISHED) { reason = map(rc); phase = State::Rollback; }
            } else {
                rc = risc_usb_claim_release_step(identity);
                if (rc == ESP_OK) {
                    released = true;
                    if (!check(clock)) return retain();
                    if (closeAfter) phase = State::Closing;
                    else return done(RISC_STREAM_OK);
                } else if (rc != ESP_ERR_NOT_FINISHED) return retain();
            }
            break;
        case State::Rollback:
            rc = risc_usb_claim_release_step(identity);
            if (!check(clock)) return expire(RISC_STREAM_TIMEOUT);
            if (rc == ESP_OK) return done(reason);
            if (rc != ESP_ERR_NOT_FINISHED) return retain();
            break;
        case State::Closing:
            began = true;
            rc = risc_usb_device_close_try(owner, dev, &closed, &deferred);
            if (!check(clock)) return expire(RISC_STREAM_TIMEOUT);
            if (closed) {
                if (rc != ESP_OK || deferred) return retain();
                return done(RISC_STREAM_OK);
            }
            if (rc != ESP_ERR_NOT_FINISHED) {
                if (released) return retain();
                return done(map(rc));
            }
            break;
        default: return retain();
        }
        if (!check(clock)) return expire(RISC_STREAM_TIMEOUT);
        return RISC_STREAM_AGAIN;
    }

    int32_t take(uint64_t token, uint8_t *out = nullptr, size_t *size = nullptr,
                 uint16_t *outVid = nullptr, uint16_t *outPid = nullptr)
    {
        if (!token || token != identity) return RISC_STREAM_INVALID;
        if (retained()) return RISC_STREAM_RETAINED;
        if (phase != State::Done) return RISC_STREAM_AGAIN;
        if (operation == Kind::Configuration && result == RISC_STREAM_OK) {
            if (!out || !size || !outVid || !outPid) return RISC_STREAM_INVALID;
            if (*size < configLength) { *size = configLength; return RISC_STREAM_LIMIT; }
            std::memcpy(out, configuration, configLength);
            *size = configLength; *outVid = vid; *outPid = pid;
        }
        const int32_t answer = result;
        phase = State::Idle; identity = 0; owner = nullptr; dev = nullptr;
        return answer;
    }
private:
    State phase = State::Idle;
    Kind operation = Kind::Configuration;
    usb_host_client_handle_t owner = nullptr;
    usb_device_handle_t dev = nullptr;
    uint64_t identity = 0, lastNow = 0, until = 0, rollbackAt = 0;
    uint8_t iface = 0, alt = 0;
    bool began = false, released = false, closed = false, deferred = false, closeAfter = false;
    int32_t result = RISC_STREAM_AGAIN, reason = RISC_STREAM_OK;
    uint8_t configuration[4096]{};
    size_t configLength = 0;
    uint16_t vid = 0, pid = 0;
    static int32_t map(esp_err_t error)
    {
        if (error == ESP_ERR_NO_MEM || error == ESP_ERR_INVALID_SIZE) return RISC_STREAM_LIMIT;
        if (error == ESP_ERR_NOT_SUPPORTED) return RISC_STREAM_UNSUPPORTED;
        if (error == ESP_ERR_NOT_FOUND) return RISC_STREAM_CLOSED;
        return RISC_STREAM_IO;
    }
    template<class Clock> bool check(Clock &clock)
    {
        if (risc_usb_admission_faulted()) { retain(); return false; }
        uint64_t now = clock.now();
        if (now < lastNow) { retain(); return false; }
        lastNow = now; return now < until;
    }
    int32_t done(int32_t value) { phase = State::Done; result = value; return value; }
    int32_t retain()
    {
        if (operation == Kind::Claim || operation == Kind::Release) risc_usb_claim_retain(identity);
        phase = State::Retained; result = RISC_STREAM_RETAINED; return result;
    }
    int32_t expire(int32_t value)
    {
        if (retained()) return RISC_STREAM_RETAINED;
        if (operation == Kind::Configuration || (operation == Kind::Claim &&
            (!began || risc_usb_claim_abandon_unacquired(identity)))) return done(value);
        if (!began && !released && !closed) return done(value);
        return retain();
    }
};
} // namespace RiscUsbController
