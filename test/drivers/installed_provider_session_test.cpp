#include "runtime/drivers/InstalledProviderSession.h"
#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
unsigned acquired = 0, released = 0, probed = 0;
bool rejectFirst = false, failFirstRelease = false;
bool failedAcquireWithGrant = false, failedAcquireWithoutGrant = false;
int first = 11, second = 22;
}

namespace RuntimeInstalledProviders {
bool nextProvider(const char* capability, uint32_t version, size_t* cursor,
                  char* id, size_t capacity) {
  assert(capability && std::strcmp(capability, "serial.port") == 0);
  assert(version == 1 && cursor && id && capacity >= 32);
  if (*cursor >= 2) return false;
  std::snprintf(id, capacity, "candidate.%lu", static_cast<unsigned long>((*cursor)++));
  return true;
}
bool acquire(const char* id, const char* capability, uint32_t version, Lease* out) {
  assert(id && capability && version == 1 && out);
  ++acquired;
  *out = {};
  if (failedAcquireWithoutGrant) return false;
  out->grant = {static_cast<uint32_t>(acquired), acquired};
  if (failedAcquireWithGrant) return false;
  out->interface = acquired == 1 ? &first : &second;
  return true;
}
bool release(Lease* lease) {
  assert(lease && lease->grant.slot);
  ++released;
  if (failFirstRelease && released == 1) return false;
  *lease = {};
  return true;
}
}

RuntimeInstalledProviders::CandidateDecision probe(const void* value, void*) {
  ++probed;
  assert(value);
  if (rejectFirst && value == &first)
    return RuntimeInstalledProviders::CandidateDecision::Unsupported;
  return RuntimeInstalledProviders::CandidateDecision::Accepted;
}
void reset() {
  acquired = released = probed = 0;
  rejectFirst = failFirstRelease = false;
  failedAcquireWithGrant = failedAcquireWithoutGrant = false;
}

int main() {
  using namespace RuntimeInstalledProviders;
  size_t cursor = 0;
  {
    SelectedSession session;
    assert(session.select("serial.port", 1, &cursor, probe, nullptr) ==
           SelectionResult::Selected);
    assert(session.acquired() && session.interface() == &first);
    assert(session.select("serial.port", 1, &cursor, probe, nullptr) ==
           SelectionResult::Fault && acquired == 1);
    assert(session.releaseChecked() && !session.acquired() && !session.faulted());
  }

  // A rejected provider whose physical quiescence is uncertain cannot be
  // replaced by the next candidate. Retry the SAME exact grant first.
  reset(); cursor = 0;
  rejectFirst = failFirstRelease = true;
  {
    SelectedSession session;
    assert(session.select("serial.port", 1, &cursor, probe, nullptr) ==
           SelectionResult::Fault);
    assert(session.acquired() && session.faulted() && !session.interface());
    assert(cursor == 1 && acquired == 1 && released == 1);
    assert(session.select("serial.port", 1, &cursor, probe, nullptr) ==
           SelectionResult::Fault && acquired == 1);
    assert(session.releaseChecked() && !session.faulted() && !session.acquired());
    assert(session.select("serial.port", 1, &cursor, probe, nullptr) ==
           SelectionResult::Selected);
    assert(session.interface() == &second && acquired == 2);
    assert(session.releaseChecked());
  }

  // Interface acquisition can return false AND an exact retained grant.
  // The recovery path must not forget it or expose its null interface.
  reset(); cursor = 0;
  failedAcquireWithGrant = true;
  {
    SelectedSession session;
    assert(session.select("serial.port", 1, &cursor, probe, nullptr) ==
           SelectionResult::Fault);
    assert(session.acquired() && session.faulted() && !session.interface());
    assert(probed == 0 && acquired == 1);
    assert(session.releaseChecked() && released == 1);
  }

  // A failed activation WITHOUT a grant may retain untracked physical state.
  // It cannot be silently reset by the session's ordinary release path.
  reset(); cursor = 0;
  failedAcquireWithoutGrant = true;
  {
    SelectedSession session;
    assert(session.select("serial.port", 1, &cursor, probe, nullptr) ==
           SelectionResult::Fault);
    assert(!session.acquired() && session.faulted());
    assert(!session.releaseChecked());
    assert(session.select("serial.port", 1, &cursor, probe, nullptr) ==
           SelectionResult::Fault && acquired == 1 && released == 0);
  }
  std::puts("Generic installed-provider session ownership tests passed");
}
