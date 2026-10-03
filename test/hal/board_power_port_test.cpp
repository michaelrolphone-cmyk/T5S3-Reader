// Compile the real NativeBoardPowerPort.cpp and public ABI/board headers.
// Only installed-graph lifetime results and provider callbacks are simulated.
#include <BoardPowerPort.h>
#include <BoardT5S3.h>
#include <RiscUsbVbusV1.h>
#include "runtime/drivers/InstalledProviderGraph.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
using RuntimeInstalledProviders::Lease;
struct Mapping {
  bool occupied = false;
  bool pending = false;
  risc_usb_vbus_charger_api_v1 api{};
};
std::array<Mapping, 16> mappings{};
std::vector<RuntimeProviders::GrantV2> releases;
unsigned acquisitions = 0, enumerations = 0, reads = 0, configurations = 0, shutdowns = 0;
unsigned releaseFailures = 0;
bool acquireFailsWithGrant = false, acquireFailsWithoutGrant = false;
bool invalidInterface = false, snapshotOkay = true, configureUncertain = false;
bool releaseBlocked = false, shutdownAccepted = false, shutdownUncertain = false;

Mapping& active(void* context) {
  const size_t slot = reinterpret_cast<uintptr_t>(context);
  assert(slot && slot < mappings.size());
  auto& mapping = mappings[slot];
  assert(mapping.occupied && !mapping.pending); // No revoked callback allowed.
  return mapping;
}
bool readSnapshot(void* context, risc_bq25896_charger_snapshot_v1* out) {
  (void)active(context);
  assert(out);
  ++reads;
  if (!snapshotOkay) return false;
  *out = {};
  out->external_power = true;
  return true;
}
bool configure(void* context) {
  (void)active(context);
  ++configurations;
  if (configureUncertain) releaseBlocked = true;
  return !configureUncertain;
}
bool requestShutdown(void* context) {
  (void)active(context);
  ++shutdowns;
  if (shutdownUncertain) releaseBlocked = true;
  return shutdownAccepted;
}
bool sameGrant(const RuntimeProviders::GrantV2& a, const RuntimeProviders::GrantV2& b) {
  return a.slot == b.slot && a.generation == b.generation;
}
void blockedOperationsMakeNoCallbacksOrLoads() {
  const auto oldAcquisitions = acquisitions, oldReads = reads;
  const auto oldConfigurations = configurations, oldShutdowns = shutdowns;
  const auto oldEnumerations = enumerations;
  const auto oldReleases = releases.size();
  bool external = false;
  BoardT5S3::BatteryState state{};
  assert(!BoardPowerPort::externalPower(&external));
  assert(!BoardPowerPort::read(&state));
  assert(!BoardPowerPort::configure());
  assert(!BoardPowerPort::prepareShutdown());
  assert(!BoardPowerPort::shutdown());
  assert(!external);
  assert(acquisitions == oldAcquisitions && reads == oldReads &&
         configurations == oldConfigurations && shutdowns == oldShutdowns &&
         enumerations == oldEnumerations && releases.size() == oldReleases);
}
} // namespace

namespace RuntimeInstalledProviders {
bool nextProvider(const char* capability, uint32_t version, size_t* cursor,
                  char* id, size_t capacity) {
  ++enumerations;
  assert(!std::strcmp(capability, "board.power.vbus") && version == 1);
  // Recovery must precede any new inventory or admission attempt.
  for (const auto& mapping : mappings) assert(!mapping.occupied || !mapping.pending);
  if ((*cursor)++) return false;
  assert(capacity > std::strlen("test-power"));
  std::strcpy(id, "test-power");
  return true;
}
bool acquire(const char* id, const char* capability, uint32_t version, Lease* out) {
  assert(!std::strcmp(id, "test-power") && !std::strcmp(capability, "board.power.vbus") && version == 1);
  ++acquisitions;
  *out = {};
  if (acquireFailsWithoutGrant) return false;
  assert(acquisitions < mappings.size());
  auto& mapping = mappings[acquisitions];
  mapping.occupied = true;
  mapping.api.base = {RISC_USB_VBUS_API_V1, sizeof(mapping.api),
                     reinterpret_cast<void*>(static_cast<uintptr_t>(acquisitions)),
                     nullptr, nullptr, nullptr};
  mapping.api.read_charger = readSnapshot;
  mapping.api.configure_charger = configure;
  mapping.api.request_shutdown = requestShutdown;
  *out = {{acquisitions, 100u + acquisitions}, &mapping.api};
  if (acquireFailsWithGrant) {
    mapping.pending = true;
    out->interface = reinterpret_cast<const void*>(uintptr_t{1});
    return false; // Deliberately unusable; bridge must never dereference it.
  }
  if (invalidInterface) mapping.api.base.struct_size = sizeof(risc_usb_vbus_monitor_api_v1);
  return true;
}
bool release(Lease* lease) {
  assert(lease && lease->grant.slot && lease->grant.slot < mappings.size());
  auto& mapping = mappings[lease->grant.slot];
  assert(mapping.occupied && lease->grant.generation == 100u + lease->grant.slot);
  if (mapping.pending) assert(lease->interface == nullptr);
  releases.push_back(lease->grant);
  mapping.pending = true;
  if (releaseBlocked) return false;
  if (releaseFailures) { --releaseFailures; return false; }
  mapping.occupied = false;
  *lease = {};
  return true;
}
} // namespace RuntimeInstalledProviders

int main(int argc, char** argv) {
  assert(argc == 2);
  const char* scenario = argv[1];
  bool external = false;
  if (!std::strcmp(scenario, "transient-release")) {
    releaseFailures = 2;
    assert(!BoardPowerPort::externalPower(&external));
    assert(!external && reads == 1 && acquisitions == 1 && releases.size() == 1);
    assert(!BoardPowerPort::configure());
    assert(configurations == 0 && acquisitions == 1 && releases.size() == 2);
    assert(sameGrant(releases[0], releases[1]));
    assert(BoardPowerPort::externalPower(&external));
    assert(external && reads == 2 && acquisitions == 2 && releases.size() == 4);
    assert(sameGrant(releases[0], releases[2]));
    assert(!sameGrant(releases[2], releases[3]));
  } else if (!std::strcmp(scenario, "failed-acquire")) {
    acquireFailsWithGrant = true;
    assert(!BoardPowerPort::externalPower(&external));
    assert(acquisitions == 1 && reads == 0 && releases.empty());
    acquireFailsWithGrant = false;
    releaseFailures = 1;
    assert(!BoardPowerPort::configure());
    assert(acquisitions == 1 && configurations == 0 && releases.size() == 1);
    assert(BoardPowerPort::configure());
    assert(acquisitions == 2 && configurations == 1 && releases.size() == 3);
    assert(sameGrant(releases[0], releases[1]));
  } else if (!std::strcmp(scenario, "invalid-interface")) {
    invalidInterface = true;
    releaseFailures = 1;
    assert(!BoardPowerPort::configure());
    assert(configurations == 0 && releases.size() == 1);
    invalidInterface = false;
    assert(BoardPowerPort::configure());
    assert(configurations == 1 && releases.size() == 3);
    assert(sameGrant(releases[0], releases[1]));
  } else if (!std::strcmp(scenario, "uncertain-configure")) {
    configureUncertain = true;
    assert(!BoardPowerPort::configure());
    assert(configurations == 1 && releases.size() == 1);
    for (unsigned i = 0; i < 4; ++i) {
      assert(!BoardPowerPort::externalPower(&external));
      assert(releases.size() == i + 2);
      assert(sameGrant(releases.front(), releases.back()));
    }
    assert(acquisitions == 1 && configurations == 1 && reads == 0 && !external);
    assert(mappings[1].occupied && mappings[1].pending);
  } else if (!std::strcmp(scenario, "snapshot-failure")) {
    snapshotOkay = false;
    external = true;
    assert(!BoardPowerPort::externalPower(&external));
    assert(external && reads == 1 && releases.size() == 1 && !mappings[1].occupied);
    snapshotOkay = true;
    BoardT5S3::BatteryState state{};
    assert(BoardPowerPort::read(&state));
    assert(state.chargerReadOk && state.vbusConnected);
  } else if (!std::strcmp(scenario, "shutdown-accepted")) {
    assert(BoardPowerPort::prepareShutdown());
    assert(BoardPowerPort::prepareShutdown());
    assert(acquisitions == 1 && releases.empty());
    shutdownAccepted = true;
    assert(BoardPowerPort::shutdown());
    assert(shutdowns == 1 && mappings[1].occupied && releases.empty());
    blockedOperationsMakeNoCallbacksOrLoads();
  } else if (!std::strcmp(scenario, "shutdown-uncertain")) {
    assert(BoardPowerPort::prepareShutdown());
    shutdownUncertain = true;
    assert(!BoardPowerPort::shutdown());
    assert(shutdowns == 1 && releases.size() == 1 && mappings[1].occupied);
    releaseBlocked = false; // Even apparent recovery must not retry BATFET cleanup.
    blockedOperationsMakeNoCallbacksOrLoads();
  } else if (!std::strcmp(scenario, "shutdown-rejected")) {
    assert(!BoardPowerPort::shutdown());
    assert(shutdowns == 1 && releases.size() == 1 && !mappings[1].occupied);
    assert(BoardPowerPort::configure()); // A definitely safe rejection is recoverable.
  } else if (!std::strcmp(scenario, "cancel-retry")) {
    assert(BoardPowerPort::prepareShutdown());
    releaseFailures = 1;
    assert(!BoardPowerPort::cancelShutdown());
    assert(mappings[1].occupied && mappings[1].pending);
    assert(!BoardPowerPort::prepareShutdown());
    assert(!BoardPowerPort::shutdown());
    assert(BoardPowerPort::cancelShutdown());
    assert(!mappings[1].occupied && acquisitions == 1 && shutdowns == 0);
    assert(BoardPowerPort::prepareShutdown());
    assert(acquisitions == 2);
    assert(BoardPowerPort::cancelShutdown());
  } else if (!std::strcmp(scenario, "prepare-failure")) {
    acquireFailsWithoutGrant = true;
    assert(!BoardPowerPort::prepareShutdown());
    assert(acquisitions == 1 && releases.empty());
    acquireFailsWithoutGrant = false;
    assert(!BoardPowerPort::prepareShutdown());
    assert(!BoardPowerPort::shutdown());
    assert(acquisitions == 1 && shutdowns == 0); // No new load after SD teardown.
  } else {
    assert(false && "Unknown test scenario");
  }
  std::printf("Board power port: %s PASS\n", scenario);
}
