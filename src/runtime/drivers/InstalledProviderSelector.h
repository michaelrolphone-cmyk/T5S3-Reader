#pragma once

#include "InstalledProviderGraph.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

// The generic runtime owns candidate enumeration, exact package acquisition
// and checked rejection. Transport-specific matching lives in the installed
// candidate and its versioned capability, not in an ID/VID/PID allowlist here.
// Call only from the serialized provider invocation owner task.
namespace RuntimeInstalledProviders {
enum class CandidateDecision : uint8_t { Unsupported, Accepted, Fault };
enum class SelectionResult : uint8_t { Selected, Exhausted, Fault };
using CandidateProbe = CandidateDecision (*)(const void* interface, void* context);

// Select the next compatible installed capability. On ANY uncertain release
// or probe fault, return the exact retained grant in `out`, stop enumeration,
// and require the caller to retry release only after physical cleanup. If an
// activation fails BEFORE a grant is issued, copy its exact installed identity
// into failedId so the graph can retry ONLY that provider's quiescence.
inline SelectionResult selectNext(const char* capability, uint32_t api,
                                  size_t* cursor, CandidateProbe probe,
                                  void* context, Lease* out,
                                  char* failedId = nullptr,
                                  size_t failedIdCapacity = 0) {
  if (failedId && failedIdCapacity) failedId[0] = 0;
  if (!out || !cursor || !probe || !capability || !*capability || !api ||
      out->grant.slot) return SelectionResult::Fault;
  *out = {};
  char id[96]{};
  while (nextProvider(capability, api, cursor, id, sizeof(id))) {
    Lease candidate{};
    if (!acquire(id, capability, api, &candidate) || !candidate.grant.slot ||
        !candidate.interface) {
      // A failed start may be mapped with no grant. Remember the identity so
      // recovery does not require global graph shutdown or guess the class.
      if (candidate.grant.slot) *out = candidate;
      if (failedId && failedIdCapacity) {
        const size_t n = std::strlen(id);
        if (n < failedIdCapacity) std::memcpy(failedId, id, n + 1);
      }
      return SelectionResult::Fault;
    }
    const CandidateDecision decision = probe(candidate.interface, context);
    if (decision == CandidateDecision::Accepted) {
      *out = candidate;
      return SelectionResult::Selected;
    }
    if (decision == CandidateDecision::Fault) {
      *out = candidate;
      return SelectionResult::Fault;
    }
    if (!release(&candidate)) {
      *out = candidate;
      return SelectionResult::Fault;
    }
  }
  return SelectionResult::Exhausted;
}
}  // namespace RuntimeInstalledProviders
