#include "native/NativeBatteryGauge.h"
#include "runtime/drivers/InstalledProviderGraph.h"
#include <BoardX4Pro.h>
#include <RiscBatteryGaugeV1.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>

thread_local bool batteryTestCritical = false;
namespace {
std::atomic<uint32_t> clockMs{0};
const std::thread::id owner = std::this_thread::get_id();
std::atomic<unsigned> acquisitions{0}, reads{0}, releases{0};
bool acquireOk = true, readOk = true, releaseOk = true, partialGrant = false;
bool nullInterface = false, noGrant = false, zeroGeneration = false;
bool releaseAttempted = false;
uint32_t generation = 11;
risc_battery_sample_v1 nextSample{3800, 67, 1};
RuntimeInstalledProviders::Lease outstanding{};

void ownerCallback() {
  assert(std::this_thread::get_id() == owner);
  assert(!batteryTestCritical);
  // Reentrant owner updates must not enter the graph or read again.
  nativeBatteryTick();
}
bool read(void*, risc_battery_sample_v1* out) {
  ownerCallback();
  ++reads;
  assert(out);
  assert(out->millivolts == 0 && out->percent == 0 && out->charging == 0);
  // A cache reader is safe even from inside a provider callback, proving that
  // callbacks never execute under the snapshot lock.
  NativeBatterySnapshot cached{};
  (void)nativeBatteryReadSnapshot(&cached);
  *out = nextSample;
  return readOk;
}
risc_battery_gauge_api_v1 api{RISC_BATTERY_GAUGE_API_V1, sizeof(api), nullptr, read};

void expectUnavailable() {
  NativeBatterySnapshot sample{1234, 99, true};
  assert(!nativeBatteryReadSnapshot(&sample));
  assert(sample.millivolts == 0 && sample.percent == 0 && !sample.charging);
  assert(!nativeBatteryReadSnapshot(nullptr));
  assert(!BoardX4Pro::beginBatteryManagement());
  assert(!BoardX4Pro::isBatteryManagementReady());
  BoardX4Pro::BatteryState state{};
  state.socPercent = 99;
  assert(!BoardX4Pro::readBatteryState(&state));
  assert(!state.gaugeReady && !state.gaugeReadOk && !state.charging && state.socPercent == 0);
  uint16_t soc = 99;
  assert(!BoardX4Pro::readBatteryStateOfCharge(&soc) && soc == 0);
}
void expectSample(uint16_t mv, uint8_t percent, bool charging) {
  NativeBatterySnapshot sample{};
  assert(nativeBatteryReadSnapshot(&sample));
  assert(sample.millivolts == mv && sample.percent == percent && sample.charging == charging);
  assert(BoardX4Pro::beginBatteryManagement());
  assert(BoardX4Pro::isBatteryManagementReady());
  BoardX4Pro::BatteryState state{};
  assert(BoardX4Pro::readBatteryState(&state));
  assert(state.gaugeReady && state.gaugeReadOk && state.socPercent == percent);
  assert(state.batteryVoltageMv == mv && state.gaugeVoltageMv == mv);
  assert(state.charging == charging);
  assert(state.gaugeState == (charging ? BoardX4Pro::BatteryGaugeState::Charge : BoardX4Pro::BatteryGaugeState::Unknown));
  assert(!state.chargerReady && !state.chargerReadOk && !state.vbusConnected && !state.chargeDone);
  assert(!state.gaugeBatteryFullFlag && !state.gaugeGaugingFullFlag && !state.chargeEnabled);
  assert(state.chargerStatus == BoardX4Pro::BatteryChargeStatus::Unknown);
  assert(state.remainingCapacityMah == 0 && state.fullCapacityMah == 0 && state.currentMa == 0);
  assert(!BoardX4Pro::capabilities().hasDetailedBatteryTelemetry);
  assert(BoardX4Pro::batteryProfile().capacityMah == 0);
  uint16_t soc = 999;
  assert(BoardX4Pro::readBatteryStateOfCharge(&soc) && soc == percent);
  assert(!BoardX4Pro::readBatteryState(nullptr));
  assert(!BoardX4Pro::readBatteryStateOfCharge(nullptr));
  int16_t current = 99;
  assert(!BoardX4Pro::readBatteryCurrentMa(&current) && current == 0);
  current = 99;
  assert(!BoardX4Pro::readBatteryAverageCurrentMa(&current) && current == 0);
  assert(!BoardX4Pro::isUsbConnected());
}
void tickAt(uint32_t now) { clockMs = now; nativeBatteryTick(); }

void normal() {
  expectUnavailable();
  assert(acquisitions == 0 && reads == 0);
  nativeBatteryTick();
  assert(acquisitions == 1 && reads == 1);
  expectSample(3800, 67, true);
  tickAt(4999);
  assert(reads == 1);
  nextSample = {3900, 100, 0};
  tickAt(5000);
  expectSample(3900, 100, false);
  nextSample = {3100, 0, 0};
  tickAt(10000);
  expectSample(3100, 0, false);  // A genuine measured 0% remains available.
  assert(acquisitions == 1 && reads == 3 && releases == 0);
}
void failedSamples() {
  nativeBatteryTick();
  expectSample(3800, 67, true);
  readOk = false;
  tickAt(5000);
  expectUnavailable();
  readOk = true;
  tickAt(9999);
  assert(reads == 2);
  expectUnavailable();
  tickAt(10000);
  expectSample(3800, 67, true);
  nextSample.percent = 101;
  tickAt(15000);
  expectUnavailable();
  nextSample.percent = 50;
  nextSample.charging = 2;
  tickAt(20000);
  expectUnavailable();
  nextSample.charging = 0;
  nextSample.millivolts = 0;
  tickAt(25000);
  expectUnavailable();
  nextSample.millivolts = 3600;
  tickAt(30000);
  expectSample(3600, 50, false);
  assert(acquisitions == 1 && releases == 0);
}
void expiry() {
  tickAt(UINT32_MAX - 4999u);
  clockMs = 9999;
  expectSample(3800, 67, true); // 14999ms including rollover.
  clockMs = 10000;
  expectUnavailable();
  clockMs = UINT32_MAX - 4999u; // A clock wrap must not resurrect expired data.
  expectUnavailable();
  clockMs = 10001;
  std::thread other([] { nativeBatteryTick(); });
  other.join();
  expectUnavailable();
  assert(acquisitions == 1 && reads == 1);
  nativeBatteryTick();
  expectSample(3800, 67, true);
  assert(reads == 2);
}
void unavailableRetry() {
  acquireOk = false;
  tickAt(UINT32_MAX - 9999u);
  for (uint32_t t = 0; t < 20000; t += 10) tickAt(t);
  assert(acquisitions == 1 && reads == 0);
  expectUnavailable();
  acquireOk = true;
  tickAt(20000);
  expectSample(3800, 67, true);
  assert(acquisitions == 2);
}
void grantlessFailure() {
  acquireOk = false;
  for (uint32_t t = 0; t <= 300000; t += 100) tickAt(t);
  expectUnavailable();
  // The graph still owns an uncertain failed-start node. The consumer cannot
  // recover it by guessing an ID, calling release without a grant, or shutting
  // down the shared graph. Only slow bounded acquisition attempts are allowed.
  assert(acquisitions == 11 && reads == 0 && releases == 0);
}
void invalidApi(const char* kind) {
  if (!std::strcmp(kind, "version")) api.api_version = 2;
  else if (!std::strcmp(kind, "size")) api.struct_size = sizeof(api) - 1;
  else if (!std::strcmp(kind, "read")) api.read = nullptr;
  else if (!std::strcmp(kind, "interface")) nullInterface = true;
  else if (!std::strcmp(kind, "generation")) zeroGeneration = true;
  else if (!std::strcmp(kind, "grant")) noGrant = true;
  else assert(false);
  nativeBatteryTick();
  assert(acquisitions == 1 && reads == 0 && releases == (noGrant ? 0u : 1u));
  expectUnavailable();
  tickAt(29999);
  assert(acquisitions == 1);
  api = {RISC_BATTERY_GAUGE_API_V1, sizeof(api), nullptr, read};
  nullInterface = noGrant = zeroGeneration = false;
  tickAt(30000);
  expectSample(3800, 67, true);
  assert(acquisitions == 2 && reads == 1);
}
void retained(bool failedAcquire) {
  acquireOk = !failedAcquire;
  partialGrant = failedAcquire;
  if (!failedAcquire) api.api_version = 0;
  releaseOk = false;
  nativeBatteryTick();
  expectUnavailable();
  assert(acquisitions == 1 && reads == 0);
  assert(releases == (failedAcquire ? 0u : 1u));
  const auto token = outstanding.grant;
  tickAt(29999);
  assert(releases == (failedAcquire ? 0u : 1u));
  tickAt(30000);
  assert(acquisitions == 1 && outstanding.grant.slot == token.slot && outstanding.grant.generation == token.generation);
  releaseOk = true;
  tickAt(60000);
  assert(outstanding.grant.slot == 0 && acquisitions == 1 && reads == 0);
  acquireOk = true;
  partialGrant = false;
  api.api_version = RISC_BATTERY_GAUGE_API_V1;
  tickAt(89999);
  assert(acquisitions == 1);
  tickAt(90000);
  expectSample(3800, 67, true);
  assert(acquisitions == 2 && reads == 1 && outstanding.grant.generation != token.generation);
}
void concurrentReaders() {
  nativeBatteryTick();
  std::atomic<bool> stop{false};
  std::atomic<unsigned> snapshots{0};
  auto observer = [&] {
    while (!stop.load()) {
      nativeBatteryTick(); // Wrong owner: must never call the graph/provider.
      NativeBatterySnapshot sample{};
      if (nativeBatteryReadSnapshot(&sample)) {
        assert((sample.percent == 67 && sample.millivolts == 3800 && sample.charging) ||
               (sample.millivolts == 3000 + sample.percent && sample.charging == ((sample.percent & 1) != 0)));
      } else assert(sample.millivolts == 0 && sample.percent == 0 && !sample.charging);
      (void)BoardX4Pro::beginBatteryManagement();
      BoardX4Pro::BatteryState state{};
      (void)BoardX4Pro::readBatteryState(&state);
      ++snapshots;
    }
  };
  std::thread a(observer), b(observer);
  for (uint32_t t = 1; t <= 10000; ++t) {
    const uint8_t percent = static_cast<uint8_t>(t % 101);
    nextSample = {static_cast<uint16_t>(3000 + percent), percent, static_cast<uint8_t>(percent & 1)};
    tickAt(t * 5000);
  }
  stop = true;
  a.join(); b.join();
  assert(snapshots > 0 && acquisitions == 1 && reads == 10001 && releases == 0);
}
} // namespace

unsigned long millis() { return clockMs.load(); }
TaskHandle_t xTaskGetCurrentTaskHandle() { thread_local int token; return &token; }
namespace RuntimeInstalledProviders {
bool acquireCapability(const char* capability, uint32_t version, Lease* out) {
  ownerCallback();
  assert(!std::strcmp(capability, "board.battery") && version == RISC_BATTERY_GAUGE_API_V1);
  assert(out && !outstanding.grant.slot && !out->grant.slot);
  ++acquisitions;
  releaseAttempted = false;
  *out = {};
  if (acquireOk || partialGrant) {
    out->grant = {noGrant ? 0u : 3u, zeroGeneration ? 0u : ++generation};
    out->interface = nullInterface ? nullptr : &api;
    outstanding = *out;
  }
  return acquireOk;
}
bool release(Lease* lease) {
  ownerCallback();
  assert(lease && lease->grant.slot == outstanding.grant.slot &&
         lease->grant.generation == outstanding.grant.generation);
  if (releaseAttempted) assert(!lease->interface); // Revoked callback discarded.
  releaseAttempted = true;
  ++releases;
  if (releaseOk) { *lease = {}; outstanding = {}; }
  return releaseOk;
}
} // namespace RuntimeInstalledProviders

int main(int argc, char** argv) {
  assert(argc >= 2);
  const char* test = argv[1];
  if (!std::strcmp(test, "normal")) normal();
  else if (!std::strcmp(test, "failures")) failedSamples();
  else if (!std::strcmp(test, "expiry")) expiry();
  else if (!std::strcmp(test, "retry")) unavailableRetry();
  else if (!std::strcmp(test, "grantless")) grantlessFailure();
  else if (!std::strcmp(test, "invalid")) { assert(argc == 3); invalidApi(argv[2]); }
  else if (!std::strcmp(test, "partial")) retained(true);
  else if (!std::strcmp(test, "release")) retained(false);
  else if (!std::strcmp(test, "threads")) concurrentReaders();
  else assert(false);
  std::printf("native battery %s: PASS\n", test);
}
