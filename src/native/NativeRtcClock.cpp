#include "NativeRtcClock.h"

#if defined(BOARD_XTEINK_X4_PRO)
#include "runtime/drivers/InstalledProviderGraph.h"
#include <Logging.h>
#include <RiscRtcCalendarV2.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
RuntimeInstalledProviders::Lease lease{};
TaskHandle_t ownerTask = nullptr;
portMUX_TYPE ownerMux = portMUX_INITIALIZER_UNLOCKED;
bool servicing = false;
bool suspended = false;

bool enterOwner(bool bind = false) {
    const TaskHandle_t current = xTaskGetCurrentTaskHandle();
    if (!current) return false;
    portENTER_CRITICAL(&ownerMux);
    if (bind && !ownerTask) ownerTask = current;
    const bool accepted = current == ownerTask && !servicing;
    if (accepted) servicing = true;
    portEXIT_CRITICAL(&ownerMux);
    return accepted;
}
struct OwnerTurn {
    ~OwnerTurn() {
        portENTER_CRITICAL(&ownerMux);
        servicing = false;
        portEXIT_CRITICAL(&ownerMux);
    }
};
bool release() {
    const bool ok = !lease.grant.slot || RuntimeInstalledProviders::release(&lease);
    // A failed release revokes use but keeps its exact generation/token.
    lease.interface = nullptr;
    if (!ok) LOG_ERR("CLK", "RTC provider release pending; sleep refused");
    return ok;
}
const risc_rtc_clock_api_v2* acquire() {
    if (suspended) return nullptr;
    if (lease.grant.slot && !release()) return nullptr;
    // Persist even a partial failed-acquire grant so it can never be lost.
    if (!RuntimeInstalledProviders::acquireCapability(
            RISC_RTC_CLOCK_CAPABILITY, RISC_RTC_CLOCK_API_V2, &lease)) {
        lease.interface = nullptr;
        LOG_DBG("CLK", "RTC capability unavailable: %s", RuntimeInstalledProviders::lastError());
        return nullptr;
    }
    const auto* api = static_cast<const risc_rtc_clock_api_v2*>(lease.interface);
    if (!lease.grant.slot || !lease.grant.generation || !api ||
        api->api_version != RISC_RTC_CLOCK_API_V2 || api->struct_size < sizeof(*api) ||
        !api->read || !api->write || !api->alarm || !api->alarm_pending) {
        (void)release();
        LOG_ERR("CLK", "RTC capability has invalid API2 table");
        return nullptr;
    }
    return api;
}
}

bool nativeRtcBegin() {
    if (!enterOwner(true)) return false;
    const OwnerTurn turn{};
    const bool ok = acquire() != nullptr;
    return release() && ok;
}
bool nativeRtcRead(risc_rtc_time_v2* out) {
    if (!out || !enterOwner()) return false;
    const OwnerTurn turn{};
    const auto* api = acquire();
    if (!api) return false;
    risc_rtc_time_v2 sample{};
    const bool ok = api->read(api->context, &sample) && risc_rtc_valid_time_v2(&sample);
    const bool released = release();
    if (!ok) LOG_ERR("CLK", "RTC read rejected: I/O, stopped clock, voltage loss, or invalid calendar");
    if (!ok || !released) return false;
    *out = sample;
    return true;
}
bool nativeRtcWrite(const risc_rtc_time_v2* value) {
    if (!risc_rtc_valid_time_v2(value) || !enterOwner()) return false;
    const OwnerTurn turn{};
    const auto* api = acquire();
    if (!api) return false;
    const bool ok = api->write(api->context, value);
    const bool released = release();
    if (!ok) LOG_ERR("CLK", "RTC write rejected or I/O failed");
    return ok && released;
}
bool nativeRtcSuspend() {
    if (!enterOwner(true)) return false;
    const OwnerTurn turn{};
    suspended = true;
    return release();
}
bool nativeRtcResume() {
    if (!enterOwner(true)) return false;
    const OwnerTurn turn{};
    if (!release()) return false;
    suspended = false;
    return true;
}
#else
bool nativeRtcBegin() { return false; }
bool nativeRtcRead(risc_rtc_time_v2*) { return false; }
bool nativeRtcWrite(const risc_rtc_time_v2*) { return false; }
bool nativeRtcSuspend() { return true; }
bool nativeRtcResume() { return true; }
#endif
