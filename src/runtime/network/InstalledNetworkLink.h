#pragma once

#include <RiscNetworkLinkV1.h>
#include <cstring>
#include "runtime/drivers/InstalledProviderGraph.h"

namespace RuntimeNetwork {
// Persistent owner-task custody. The consumer must keep this object alive
// until releaseChecked succeeds; an RAII destructor cannot discard failure.
// The native HTTP consumer stores it in firmware lifetime, not an app stack.
class InstalledLink final {
 public:
  InstalledLink() = default;
  InstalledLink(const InstalledLink&) = delete;
  InstalledLink& operator=(const InstalledLink&) = delete;

  bool bind(const char* providerId) {
    if (hasCustody() || !providerId) return false;
    size_t n = 0;
    while (n < sizeof(provider_) && providerId[n]) ++n;
    if (!n || n == sizeof(provider_)) return false;
    std::memcpy(provider_, providerId, n + 1);
    closing_ = true; // Failed activation can retain a graph node without a grant.
    if (!RuntimeInstalledProviders::acquire(provider_, RISC_NETWORK_LINK_CAPABILITY,
                                            RISC_NETWORK_LINK_API_V1, &lease_)) return false;
    const auto* next = static_cast<const risc_network_link_v1*>(lease_.interface);
    if (!lease_.grant.slot || !next || next->api_version != RISC_NETWORK_LINK_API_V1 ||
        next->struct_size < RISC_NETWORK_LINK_V1_SIZE ||
        !next->begin_use || !next->state || !next->end_use) return false;
    api_ = next;
    closing_ = false;
    return true;
  }

  bool beginUse(uint32_t flags) {
    if (!api_ || closing_ || token_) return false;
    const int32_t result = api_->begin_use(api_->context, flags, &token_);
    if (result == RISC_NETWORK_LINK_READY && token_) return true;
    // A partial begin owns its exact activity. Even malformed READY-with-zero
    // must go through generic quiesce, not be presented as healthy admission.
    closing_ = true;
    return false;
  }

  bool state(risc_network_link_state_v1* out) const {
    if (!out) return false;
    *out = {};
    out->struct_size = sizeof(*out);
    if (!api_ || closing_ || !api_->state(api_->context, token_, out)) {
      *out = {};
      return false;
    }
    if (out->struct_size < sizeof(*out) || out->connected > 1 || out->has_address > 1 ||
        out->reserved[0] || out->reserved[1] ||
        !std::memchr(out->address, 0, sizeof(out->address))) {
      *out = {};
      return false;
    }
    return true;
  }

  bool endUseChecked() {
    if (!token_) return true;
    if (!api_ || api_->end_use(api_->context, token_) != RISC_NETWORK_LINK_READY) {
      closing_ = true;
      return false;
    }
    token_ = 0;
    // A failed end/begin remains closed until explicit full release, so it
    // cannot silently turn a recovered old invocation into a new consumer.
    return true;
  }

  bool releaseChecked() {
    if (!hasCustody()) return true;
    closing_ = true;
    if (!endUseChecked()) return false;
    // Once graph release begins it revokes the interface. Never call it again
    // on release retry, even if the graph keeps the same mapped address.
    api_ = nullptr;
    if (lease_.grant.slot) {
      if (!RuntimeInstalledProviders::release(&lease_)) return false;
    } else if (!RuntimeInstalledProviders::recoverFailedProvider(
                   provider_, RISC_NETWORK_LINK_CAPABILITY, RISC_NETWORK_LINK_API_V1)) {
      return false;
    }
    provider_[0] = 0;
    closing_ = false;
    return true;
  }

  bool hasCustody() const { return provider_[0] != 0; }
  bool closing() const { return closing_; }

 private:
  RuntimeInstalledProviders::Lease lease_{};
  const risc_network_link_v1* api_ = nullptr;
  uint64_t token_ = 0;
  char provider_[96]{};
  bool closing_ = false;
};
}  // namespace RuntimeNetwork
