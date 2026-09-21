#pragma once

#include "InstalledProviderSelector.h"
#include <cstring>

// Generic invocation-owner state for one selected installed capability.
// Rejection, activation and release are entirely independent of a particular
// transport. A fault without a grant can still retain physical state, so its
// exact provider identity is retained for targeted graph recovery.
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
    char failedId[96]{};
    const SelectionResult result = selectNext(capability, version, cursor,
                                               probe, context, &next,
                                               failedId, sizeof(failedId));
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
      // On a grantless fault, retain a bounded immutable copy of the actual
      // failing candidate ID; a later retry must never target a different ELF.
      if (!next.grant.slot && failedId[0] && capability) {
        const size_t length = std::strlen(capability);
        if (length && length < sizeof(capability_)) {
          std::memcpy(failedProvider_, failedId, sizeof(failedProvider_));
          std::memcpy(capability_, capability, length + 1);
          version_ = version;
        }
      }
    }
    return result;
  }

  // Physical cleanup is performed by the graph. Never discard a grant just
  // because a candidate cannot provide a callable interface or is closing.
  // Without a grant, recover the exact failed activation in the graph; neither
  // path restarts a provider while its old DMA/claims might still be live.
  bool releaseChecked() {
    if (!lease_.grant.slot) {
      if (!faulted_) return true;
      if (!failedProvider_[0] || !capability_[0] || !version_ ||
          !recoverFailedProvider(failedProvider_, capability_, version_))
        return false;
      faulted_ = false;
      failedProvider_[0] = capability_[0] = 0;
      version_ = 0;
      return true;
    }
    if (!release(&lease_)) {
      faulted_ = true;
      return false;
    }
    faulted_ = false;
    failedProvider_[0] = capability_[0] = 0;
    version_ = 0;
    return true;
  }

  bool acquired() const { return lease_.grant.slot != 0; }
  bool faulted() const { return faulted_; }
  const void* interface() const { return faulted_ ? nullptr : lease_.interface; }

 private:
  Lease lease_{};
  bool faulted_ = false;
  char failedProvider_[96]{};
  char capability_[64]{};
  uint32_t version_ = 0;
};
} // namespace RuntimeInstalledProviders
