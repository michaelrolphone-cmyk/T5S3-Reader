#pragma once
#include "ProviderModuleV2.h"
#include <cstddef>
#include <cstdint>

/* Generic resolver inputs MUST originate from independently integrity/trust-
 * validated installed manifests. Strings and requirement arrays must remain
 * valid while the graph exists. No hardware identifiers have special meaning.
 * This is a bounded graph / module-lifetime primitive, not a permission API. */
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
  GrantV2 acquire(const char* capability, uint32_t api);
  bool release(GrantV2 grant);
  const void* interfaceFor(GrantV2 grant) const;
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
  bool activate(size_t index);
  void releaseDependencies(size_t index);
  bool deactivateIfUnused(size_t index);
};
}  // namespace RuntimeProviders
