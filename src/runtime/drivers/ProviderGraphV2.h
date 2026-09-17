#pragma once
#include "ProviderModuleV2.h"
#include <cstddef>
#include <cstdint>

/* Generic resolver inputs MUST originate from independently integrity/trust-
 * validated installed manifests. Strings, requirements and verified image
 * bytes must remain valid/immutable while the graph can activate the node.
 * No hardware capability identifier has special meaning in this graph.
 * This is a module-lifetime primitive, NOT a signature/permission API. */
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
  /* Append-only private verified-loader admission. Zero retains the existing
   * unprivileged path. Nonzero requests a versioned privileged port ABI and
   * requires the trusted installer to supply immutable authenticated bytes,
   * the exact-import preflight, dependency pins and execution authorization.
   * This manifest field is a requirement, NEVER authority in its own right. */
  uint32_t requiredOsCpuAbi = 0;
  const uint8_t* verifiedElfBytes = nullptr;
  size_t verifiedElfLength = 0;
};
struct GrantV2 {
  uint32_t slot = 0;       // Zero is invalid.
  uint32_t generation = 0;
};

class GraphV2 final {
 public:
  static constexpr size_t kMaxModules = 16;
  static constexpr size_t kMaxGrants = 32;
  GraphV2() = default;
  GraphV2(const GraphV2&) = delete;
  GraphV2& operator=(const GraphV2&) = delete;
  bool addVerified(const SpecV2& spec);
  // An unresolved capability with competing providers fails closed rather
  // than silently preferring the first installed package.
  GrantV2 acquire(const char* capability, uint32_t api);
  // The trusted caller may choose a specific manifest-verified provider ID;
  // this does not grant an application permission or infer device semantics.
  GrantV2 acquireFrom(const char* providerId, const char* capability, uint32_t api);
  // The grant becomes stale even when hardware cannot safely quiesce. Failed
  // release quarantines the provider, preventing new grants; shutdown can
  // retry without releasing its dependency pins prematurely.
  bool release(GrantV2 grant);
  const void* interfaceFor(GrantV2 grant) const;
  // Returns true only when every provider, including failed-start and
  // failed-teardown quarantines, is fully quiesced and its dependencies are
  // released. False preserves all code and dependencies still required.
  bool shutdown();
  size_t moduleCount() const { return count_; }
  size_t liveGrants() const;

 private:
  enum class Visit : uint8_t { Idle, Visiting, Active };
  struct Node {
    SpecV2 spec{};
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
