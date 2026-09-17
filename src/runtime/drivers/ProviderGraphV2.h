#pragma once
#include "ProviderModuleV2.h"
#include <cstddef>
#include <cstdint>

/* The generic graph owns its registration metadata. A trusted package manager
 * must authenticate signer, executable digest and exact imports independently;
 * copying a caller's SpecV2 is NEVER an authorization decision. The private
 * loader hashes its own executable snapshot and validates both symbol tables.
 */
namespace RuntimeProviders {
struct RequirementV2 {
  const char* capability;
  uint32_t api;
};
struct SpecV2 {
  const char* id;
  const char* verifiedElfPath;
  const char* provides;
  uint32_t api;
  const RequirementV2* requirements;
  size_t requirementCount;
  /* Versioned generic OS/CPU services, not a built-in hardware driver.
   * Zero retains the existing unprivileged loading path. */
  uint32_t requiredOsCpuAbi = 0;
  const uint8_t* verifiedElfBytes = nullptr;
  size_t verifiedElfLength = 0;
  /* Must originate in a cryptographically authenticated package receipt.
   * Names are sorted, unique, and must match BOTH ELF symbol tables. */
  const char* const* signedImports = nullptr;
  size_t signedImportCount = 0;
  /* The manager must copy this digest from authenticated package content. */
  uint8_t authenticatedElfSha256[32] = {};
};
struct GrantV2 {
  uint32_t slot = 0;
  uint32_t generation = 0;
};

struct OwnedNodeV2;
class GraphV2 final {
 public:
  static constexpr size_t kMaxModules = 16;
  static constexpr size_t kMaxGrants = 32;
  GraphV2() = default;
  GraphV2(const GraphV2&) = delete;
  GraphV2& operator=(const GraphV2&) = delete;
  ~GraphV2();
  /* Existing trusted-manifest registration; this API does not verify signers.
   * Until the signed manager's private admission is integrated, privileged
   * registration must not be exposed to ordinary app-controlled callers. */
  bool addVerified(const SpecV2& spec);
  // Ambiguous providers fail closed rather than using install order.
  GrantV2 acquire(const char* capability, uint32_t api);
  GrantV2 acquireFrom(const char* providerId, const char* capability, uint32_t api);
  bool release(GrantV2 grant);
  const void* interfaceFor(GrantV2 grant) const;
  // Never unmap a provider whose teardown/quiescence failed.
  bool shutdown();
  size_t moduleCount() const { return count_; }
  size_t liveGrants() const;

 private:
  enum class Visit : uint8_t { Idle, Visiting, Active };
  struct Node {
    SpecV2 spec{};
    OwnedNodeV2* owned = nullptr;
    ModuleV2 module;
    Visit visit = Visit::Idle;
    uint8_t dependencies[kMaxModules]{};
    size_t acquired = 0;
  };
  struct GrantSlot {
    uint32_t generation = 0;
    uint8_t node = 0;
    bool occupied = false;
  };
  Node nodes_[kMaxModules]{};
  GrantSlot grants_[kMaxGrants]{};
  size_t count_ = 0;
  uint32_t nextGeneration_ = 0;

  int find(const char* capability, uint32_t api) const;
  int findProvider(const char* id, const char* capability, uint32_t api) const;
  GrantV2 acquireIndex(size_t index);
  bool activate(size_t index);
  void releaseDependencies(size_t index);
  bool deactivateIfUnused(size_t index);
};
}  // namespace RuntimeProviders
