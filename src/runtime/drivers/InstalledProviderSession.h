#pragma once

#include "InstalledProviderSelector.h"

// Generic invocation-owner state for one selected installed capability.
// Rejection, activation and release are entirely independent of a particular
// transport. A fault with no returned grant is still a fault: a provider may
// retain physical state after its activation failed, and no other candidate
// may be tried until the graph itself has proven recovery.
namespace RuntimeInstalledProviders {
class SelectedSession final {
 public:
  SelectedSession() = default;
  SelectedSession(const SelectedSession&) = delete;
  SelectedSession& operator=(const SelectedSession&) = delete;

  SelectionResult select(const char* capability, uint32_t version,
                         size_t* cursor, CandidateProbe probe, void* context) {
    if (faulted_ || lease_.grant.slot) return SelectionResult::Fault;
    Lease next{};
    const SelectionResult result = selectNext(capability, version, cursor,
                                               probe, context, &next);
    if (result == SelectionResult::Selected) {
      if (!next.grant.slot || !next.interface) {
        lease_ = next;
        faulted_ = true;
        return SelectionResult::Fault;
      }
      lease_ = next;
    } else if (result == SelectionResult::Fault) {
      lease_ = next; // Exact generation retained even without an interface.
      faulted_ = true;
    }
    return result;
  }

  // Physical cleanup is performed by the graph. Never discard a grant just
  // because a candidate cannot provide a callable interface or is closing.
  bool releaseChecked() {
    if (!lease_.grant.slot) return !faulted_;
    if (!release(&lease_)) {
      faulted_ = true;
      return false;
    }
    faulted_ = false;
    return true;
  }

  bool acquired() const { return lease_.grant.slot != 0; }
  bool faulted() const { return faulted_; }
  const void* interface() const { return faulted_ ? nullptr : lease_.interface; }

 private:
  Lease lease_{};
  bool faulted_ = false;
};
} // namespace RuntimeInstalledProviders
