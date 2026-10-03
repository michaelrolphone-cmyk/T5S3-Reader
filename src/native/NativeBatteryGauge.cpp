#include "NativeBatteryGauge.h"

#if defined(BOARD_XTEINK_X4_PRO)
#include "runtime/drivers/InstalledProviderGraph.h"

#include <Arduino.h>
#include <Logging.h>
#include <RiscBatteryGaugeV1.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
constexpr uint32_t kPollMs = 5000;
constexpr uint32_t kSampleTtlMs = 15000;
constexpr uint32_t kRetryMs = 30000;

// Only the owner accesses the lease, API and retry state. The short lock below
// protects task binding and copied data only, never a provider/graph callback.
RuntimeInstalledProviders::Lease lease{};
const risc_battery_gauge_api_v1* api = nullptr;
bool attempted = false;
bool polled = false;
uint32_t lastAttemptMs = 0;
uint32_t lastPollMs = 0;
TaskHandle_t ownerTask = nullptr;
bool servicing = false;
bool suspended = false;
portMUX_TYPE snapshotMux = portMUX_INITIALIZER_UNLOCKED;
NativeBatterySnapshot snapshot{};
uint32_t sampledAtMs = 0;
bool available = false;

bool enterOwner() {
  const TaskHandle_t current = xTaskGetCurrentTaskHandle();
  if (!current) return false;
  portENTER_CRITICAL(&snapshotMux);
  if (!ownerTask) ownerTask = current;
  const bool accepted = current == ownerTask && !servicing;
  if (accepted) servicing = true;
  portEXIT_CRITICAL(&snapshotMux);
  return accepted;
}

struct OwnerTurn {
  ~OwnerTurn() {
    portENTER_CRITICAL(&snapshotMux);
    servicing = false;
    portEXIT_CRITICAL(&snapshotMux);
  }
};

void publish(const risc_battery_sample_v1* sample) {
  portENTER_CRITICAL(&snapshotMux);
  const bool wasAvailable = available;
  available = sample != nullptr;
  snapshot = sample ? NativeBatterySnapshot{sample->millivolts, sample->percent, sample->charging != 0}
                    : NativeBatterySnapshot{};
  sampledAtMs = static_cast<uint32_t>(millis());
  portEXIT_CRITICAL(&snapshotMux);
  if (wasAvailable != (sample != nullptr))
    LOG_INF("BATTERY", "Installed battery sample %s", sample ? "available" : "unavailable");
}

bool acquire(uint32_t now) {
  if (attempted && static_cast<uint32_t>(now - lastAttemptMs) < kRetryMs) return false;
  attempted = true;
  lastAttemptMs = now;

  if (lease.grant.slot) {
    // An invalid interface or failed acquisition can still own an exact grant.
    // Failed release revokes use but must retain the token for later cleanup.
    // Never overwrite it, call the quarantined API, or shutdown a shared graph.
    (void)RuntimeInstalledProviders::release(&lease);
    lease.interface = nullptr;  // A failed release has already revoked use.
    return false;
  }

  // Acquire directly into our persistent owner slot even on failure: the graph
  // may return a partial grant with no interface when safe cleanup fails.
  const bool acquired = RuntimeInstalledProviders::acquireCapability(
      "board.battery", RISC_BATTERY_GAUGE_API_V1, &lease);
  if (!acquired) {
    lease.interface = nullptr;  // Only a partial cleanup token may remain.
    // A grantless failed start is still graph-owned. Acquisition cannot reset
    // a quiescence-uncertain node; it remains unavailable until its lifecycle
    // owner recovers it or reboot. Never guess a provider ID or shut down peers.
    LOG_DBG("BATTERY", "Installed battery provider unavailable");
    return false;
  }
  const auto* candidate = static_cast<const risc_battery_gauge_api_v1*>(lease.interface);
  if (!lease.grant.slot || !lease.grant.generation || !candidate ||
      candidate->api_version != RISC_BATTERY_GAUGE_API_V1 ||
      candidate->struct_size < sizeof(*candidate) || !candidate->read) {
    if (lease.grant.slot) (void)RuntimeInstalledProviders::release(&lease);
    lease.interface = nullptr;
    LOG_ERR("BATTERY", "Installed battery provider exposed an invalid API");
    return false;
  }
  api = candidate;
  polled = false;
  return true;
}
}  // namespace

void nativeBatteryTick() {
  if (!enterOwner()) return;
  const OwnerTurn turn{};
  if (suspended) return;
  const uint32_t now = static_cast<uint32_t>(millis());
  if (!api && !acquire(now)) return;
  if (polled && static_cast<uint32_t>(now - lastPollMs) < kPollMs) return;
  polled = true;
  lastPollMs = now;
  risc_battery_sample_v1 sample{};
  // One fixed sample operation per turn, with no consumer-side retry loop.
  // Duration depends on the provider; this cache cannot enforce its timeout or
  // preempt a callback. Bad samples fail closed and retain the lifetime lease.
  if (!api->read(api->context, &sample) || sample.percent > 100 ||
      sample.charging > 1 || !sample.millivolts) {
    publish(nullptr);
    return;
  }
  publish(&sample);
}

bool nativeBatterySuspend() {
  if (!enterOwner()) return false;
  const OwnerTurn turn{};
  suspended = true;
  api = nullptr;
  publish(nullptr);
  const bool released = !lease.grant.slot || RuntimeInstalledProviders::release(&lease);
  lease.interface = nullptr; // Failed release revoked use, but retains its token.
  return released;
}

bool nativeBatteryResume() {
  if (!enterOwner()) return false;
  const OwnerTurn turn{};
  if (!suspended) return true;
  if (lease.grant.slot && !RuntimeInstalledProviders::release(&lease)) {
    lease.interface = nullptr;
    return false;
  }
  suspended = false;
  attempted = polled = false;
  return true; // Optional reacquisition happens on the next ordinary owner tick.
}

bool nativeBatteryReadSnapshot(NativeBatterySnapshot* out) {
  if (!out) return false;
  portENTER_CRITICAL(&snapshotMux);
  // Unsigned subtraction handles monotonic millis rollover. Latch expiry so
  // a sample cannot resurrect after a complete clock wrap without a new poll.
  if (available && static_cast<uint32_t>(millis() - sampledAtMs) >= kSampleTtlMs) {
    available = false;
    snapshot = {};
  }
  *out = snapshot;
  const bool valid = available;
  portEXIT_CRITICAL(&snapshotMux);
  return valid;
}
#else
bool nativeBatterySuspend() { return true; }
bool nativeBatteryResume() { return true; }
void nativeBatteryTick() {}
bool nativeBatteryReadSnapshot(NativeBatterySnapshot* out) {
  if (out) *out = {};
  return false;
}
#endif
