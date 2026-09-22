#pragma once
#include "ProviderModuleV2.h"
#include <cstddef>
#include <cstdint>

/* Generic graph owns all registration metadata and dependency interface
 * tables. Privileged admission belongs only to the trusted signed executor;
 * a caller-supplied digest/import set must never confer OS/CPU rights. */
namespace RuntimePackages { class DeviceProviderExecutorV2; }
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
  uint32_t requiredOsCpuAbi = 0;
  const uint8_t* verifiedElfBytes = nullptr;
  size_t verifiedElfLength = 0;
  const char* const* signedImports = nullptr;
  size_t signedImportCount = 0;
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
  // Ordinary providers only; forged privileged specs fail regardless of hash.
  bool addVerified(const SpecV2& spec);
  GrantV2 acquire(const char* capability, uint32_t api);
  GrantV2 acquireFrom(const char* providerId, const char* capability, uint32_t api);
  bool release(GrantV2 grant);
  const void* interfaceFor(GrantV2 grant) const;
  bool shutdown();
  size_t moduleCount() const { return count_; }
  size_t liveGrants() const;
  const char* lastError() const { return error_; }

 private:
  // Compiled-in firmware executor only; not an ordinary ELF export. The
  // executor must verify P-256 signer, signed entry hashes, identity, policy,
  // rollback floor and exact import declarations BEFORE entering this API.
  // Friendship is an API boundary, not a memory-isolation guarantee.
  friend class ::RuntimePackages::DeviceProviderExecutorV2;
  bool addAuthenticatedPrivileged(const SpecV2& spec);
  bool addChecked(const SpecV2& spec, bool privilegedAdmission);

  enum class Visit : uint8_t { Idle, Visiting, Active };
  struct Node {
    SpecV2 spec{};
    OwnedNodeV2* owned = nullptr;
    ModuleV2 module;
    Visit visit = Visit::Idle;
    uint8_t dependencies[kMaxModules]{};
    // start() may retain this table until quiesce/stop; unlike a temporary
    // activate() stack array, this remains valid while the ELF is mapped.
    risc_provider_dependency_v1 boundDependencies[kMaxModules]{};
    size_t acquired = 0;
  };
  struct GrantSlot {
    uint32_t generation = 0;
    uint8_t node = 0;
    bool occupied = false;
  };
  bool fail(const char* stage, const char* identity);
  char error_[160]{};
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
