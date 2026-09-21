#include "runtime/drivers/InstalledProviderSelector.h"
#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
// Simulate manifest-discovered providers; selector has no knowledge of the
// number, package names or chipset identities. Fourth class is installed
// independently and must be considered without changing the runtime.
constexpr const char* ids[] = {"class.one", "class.two", "class.three", "class.four"};
int interfaces[] = {1, 2, 3, 4};
constexpr size_t count = sizeof(ids) / sizeof(ids[0]);
int acquisitions = 0, releases = 0, probes = 0;
int failAcquireAt = 0, failReleaseAt = 0, faultProbeAt = 0, acceptProbeAt = 2;
unsigned serial = 0;
}

namespace RuntimeInstalledProviders {
bool nextProvider(const char* cap, uint32_t api, size_t* cursor, char* id, size_t capacity) {
  assert(cap && std::strcmp(cap, "serial.port") == 0 && api == 1);
  if (!cursor || !id || capacity < 16 || *cursor >= count) return false;
  std::snprintf(id, capacity, "%s", ids[(*cursor)++]);
  return true;
}
bool acquire(const char* id, const char* cap, uint32_t api, Lease* out) {
  assert(id && cap && std::strcmp(cap, "serial.port") == 0 && api == 1 && out);
  ++acquisitions;
  if (failAcquireAt == acquisitions) return false;
  for (size_t i = 0; i < count; ++i) {
    if (!std::strcmp(id, ids[i])) {
      out->grant = {static_cast<uint32_t>(i + 1), ++serial};
      out->interface = &interfaces[i];
      return true;
    }
  }
  return false;
}
bool release(Lease* lease) {
  assert(lease && lease->grant.slot);
  ++releases;
  if (releases == failReleaseAt) return false;
  *lease = {};
  return true;
}
}

RuntimeInstalledProviders::CandidateDecision probe(const void* capability, void*) {
  assert(capability);
  ++probes;
  const int index = *static_cast<const int*>(capability);
  if (faultProbeAt == index) return RuntimeInstalledProviders::CandidateDecision::Fault;
  if (acceptProbeAt == index) return RuntimeInstalledProviders::CandidateDecision::Accepted;
  return RuntimeInstalledProviders::CandidateDecision::Unsupported;
}
void reset() {
  acquisitions = releases = probes = failAcquireAt = failReleaseAt = faultProbeAt = 0;
  acceptProbeAt = 2;
}

int main() {
  using namespace RuntimeInstalledProviders;
  Lease selected{};
  size_t cursor = 0;
  assert(selectNext("serial.port", 1, &cursor, probe, nullptr, &selected) ==
         SelectionResult::Selected);
  assert(selected.grant.slot == 2 && cursor == 2 && acquisitions == 2 &&
         releases == 1 && probes == 2);
  // A live class grant may not be silently overwritten with another class.
  assert(selectNext("serial.port", 1, &cursor, probe, nullptr, &selected) ==
         SelectionResult::Fault && selected.grant.slot == 2);
  assert(release(&selected) && !selected.grant.slot);

  reset();
  cursor = 0;
  acceptProbeAt = 0;
  assert(selectNext("serial.port", 1, &cursor, probe, nullptr, &selected) ==
         SelectionResult::Exhausted);
  assert(cursor == count && acquisitions == (int)count &&
         releases == (int)count && !selected.grant.slot);

  // A FOURTH class appears only in the manifest inventory. The same generic
  // selector automatically rejects the first three then acquires the fourth.
  reset();
  cursor = 0;
  acceptProbeAt = 4;
  assert(selectNext("serial.port", 1, &cursor, probe, nullptr, &selected) ==
         SelectionResult::Selected);
  assert(selected.grant.slot == 4 && selected.interface == &interfaces[3] &&
         cursor == 4 && acquisitions == 4 && releases == 3 && probes == 4);
  assert(release(&selected));

  // Failed checked release of rejected class pins the EXACT candidate;
  // later installed classes must not be activated until retry succeeds.
  reset();
  cursor = 0;
  failReleaseAt = 1;
  assert(selectNext("serial.port", 1, &cursor, probe, nullptr, &selected) ==
         SelectionResult::Fault);
  assert(selected.grant.slot == 1 && cursor == 1 && acquisitions == 1 && probes == 1);
  failReleaseAt = 0;
  assert(release(&selected) && !selected.grant.slot);

  reset();
  cursor = 0;
  faultProbeAt = 1;
  assert(selectNext("serial.port", 1, &cursor, probe, nullptr, &selected) ==
         SelectionResult::Fault);
  assert(selected.grant.slot == 1 && releases == 0 && cursor == 1);
  assert(release(&selected));

  reset();
  cursor = 0;
  failAcquireAt = 1;
  assert(selectNext("serial.port", 1, &cursor, probe, nullptr, &selected) ==
         SelectionResult::Fault);
  assert(!selected.grant.slot && acquisitions == 1 && probes == 0 && cursor == 1);
  std::puts("Generic selector admits fourth installed class and retains faults: PASS");
}
