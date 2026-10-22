#include "runtime/network/InstalledNetworkLink.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

namespace { RuntimeProviders::GraphV2 graph; }
namespace RuntimeInstalledProviders {
bool acquire(const char* id, const char* capability, uint32_t version, Lease* out) {
  *out = {};
  out->grant = graph.acquireFrom(id, capability, version);
  out->interface = graph.interfaceFor(out->grant);
  return out->interface != nullptr;
}
bool release(Lease* lease) {
  if (!graph.release(lease->grant)) return false;
  *lease = {};
  return true;
}
AcquisitionResult acquireChecked(const char* id, const char* capability, uint32_t version, Lease* out) {
  if (acquire(id, capability, version, out)) return AcquisitionResult::Acquired;
  return graph.failedAcquisitionNeedsRecovery(id, capability, version)
      ? AcquisitionResult::Retained : AcquisitionResult::Rejected;
}
bool recoverFailedAcquisition(const char* id, const char* capability, uint32_t version) {
  return graph.recoverFailedAcquisition(id, capability, version);
}
}

int main(int argc, char** argv) {
  assert(argc == 4);
  assert(graph.addVerified({"test.network", argv[1], RISC_NETWORK_LINK_CAPABILITY,
                            RISC_NETWORK_LINK_API_V1, nullptr, 0}));
  RuntimeNetwork::InstalledLink first, second, rejected;
  assert(!rejected.bind(nullptr) && !rejected.bind(""));
  assert(!rejected.bind("not.installed") && rejected.closing());
  assert(rejected.releaseChecked() && !rejected.hasCustody());
  assert(first.bind("test.network") && second.bind("test.network"));
  assert(graph.liveGrants() == 2);
  assert(first.beginUse(RISC_NETWORK_LINK_LOW_LATENCY));
  assert(second.beginUse(RISC_NETWORK_LINK_LOW_LATENCY));
  risc_network_link_state_v1 state{};
  assert(first.state(&state) && second.state(&state));
  RuntimeProviders::GrantV2 extra[RuntimeProviders::GraphV2::kMaxGrants - 2]{};
  for (auto& grant : extra) {
    grant = graph.acquireFrom("test.network", RISC_NETWORK_LINK_CAPABILITY, 1);
    assert(grant.slot);
  }
  assert(!rejected.bind("test.network")); // Full table, another live consumer.
  assert(rejected.releaseChecked()); // No quiesce/recovery of that consumer.
  assert(first.state(&state) && second.state(&state));
  for (auto grant : extra) assert(graph.release(grant));
  assert(!first.endUseChecked()); // Exact activity remains in the provider ELF.
  assert(!first.state(&state) && first.hasCustody());
  assert(first.releaseChecked()); // Retry end succeeds, second consumer survives.
  assert(graph.liveGrants() == 1 && second.state(&state));
  assert(second.endUseChecked());
  assert(!second.releaseChecked()); // Last grant revoked; quiesce fails once.
  assert(second.hasCustody() && !second.state(&state) && graph.liveGrants() == 1);
  assert(!graph.shutdown());
  assert(!rejected.bind("test.network")); // No regrant of quarantined provider.
  assert(rejected.releaseChecked()); // Rejection owns none of that pending grant.
  assert(second.releaseChecked()); // Same graph generation retry, now unmaps.
  assert(graph.liveGrants() == 0 && rejected.releaseChecked());
  assert(graph.addVerified({"test.failed", argv[2], RISC_NETWORK_LINK_CAPABILITY,
                            RISC_NETWORK_LINK_API_V1, nullptr, 0}));
  assert(graph.addVerified({"test.dependency", argv[3], "test.transport", 1, nullptr, 0}));
  const RuntimeProviders::RequirementV2 dependency{"test.transport", 1};
  assert(graph.addVerified({"test.dependent", argv[1], RISC_NETWORK_LINK_CAPABILITY,
                            RISC_NETWORK_LINK_API_V1, &dependency, 1}));
  assert(first.bind("test.network") && first.beginUse(RISC_NETWORK_LINK_LOW_LATENCY));
  for (const char* id : {"test.failed", "test.dependent"}) {
    assert(!rejected.bind(id));
    assert(graph.failedAcquisitionNeedsRecovery(id, RISC_NETWORK_LINK_CAPABILITY, 1));
    assert(!rejected.releaseChecked()); // Failed start/dependency still mapped.
    assert(first.state(&state) && graph.liveGrants() == 1);
    assert(rejected.releaseChecked());
    assert(!graph.failedAcquisitionNeedsRecovery(id, RISC_NETWORK_LINK_CAPABILITY, 1));
    assert(first.state(&state) && graph.liveGrants() == 1);
  }
  assert(!first.releaseChecked()); // Retry activity then exact graph cleanup.
  assert(!first.releaseChecked());
  assert(first.releaseChecked());
  assert(graph.shutdown());
  puts("Installed link real ELF/GraphV2: concurrent consumers, retained activity, revoked grant and exact cleanup retry PASS");
}
