#include "ProviderGraphV2.h"
#include <cstring>

namespace RuntimeProviders {
namespace {
bool validName(const char* s) {
  if (!s || !s[0]) return false;
  size_t i = 0;
  while (s[i] && i < 95) ++i;
  return i && i < 95;
}
}

int GraphV2::find(const char* capability, uint32_t api) const {
  if (!validName(capability) || !api) return -1;
  for (size_t i = 0; i < count_; ++i)
    if (nodes_[i].spec.api == api &&
        std::strcmp(nodes_[i].spec.provides, capability) == 0)
      return static_cast<int>(i);
  return -1;
}

bool GraphV2::addVerified(const SpecV2& spec) {
  if (count_ == kMaxModules || !validName(spec.id) ||
      !validName(spec.provides) || !spec.api ||
      !spec.verifiedElfPath || spec.verifiedElfPath[0] != '/' ||
      spec.requirementCount > kMaxModules ||
      (spec.requirementCount && !spec.requirements) ||
      find(spec.provides, spec.api) >= 0) return false;
  for (size_t i = 0; i < count_; ++i) {
    if (nodes_[i].visit != Visit::Idle ||
        nodes_[i].module.state() != ModuleV2::State::Absent ||
        std::strcmp(nodes_[i].spec.id, spec.id) == 0) return false;
  }
  if (liveGrants()) return false;
  for (size_t i = 0; i < spec.requirementCount; ++i) {
    if (!validName(spec.requirements[i].capability) || !spec.requirements[i].api)
      return false;
    for (size_t j = 0; j < i; ++j)
      if (std::strcmp(spec.requirements[i].capability,
                      spec.requirements[j].capability) == 0) return false;
  }
  nodes_[count_++].spec = spec;
  return true;
}

void GraphV2::releaseDependencies(size_t index) {
  Node& node = nodes_[index];
  // Release pins in reverse acquisition order, then unload orphaned modules.
  while (node.acquired) {
    size_t dependency = node.dependencies[--node.acquired];
    (void)nodes_[dependency].module.unpinConsumer();
    (void)deactivateIfUnused(dependency);
  }
}

bool GraphV2::deactivateIfUnused(size_t index) {
  Node& node = nodes_[index];
  if (node.visit != Visit::Active || node.module.consumers()) return true;
  if (!node.module.unload()) return false;
  node.visit = Visit::Idle;
  releaseDependencies(index);
  return true;
}

bool GraphV2::activate(size_t index) {
  Node& node = nodes_[index];
  if (node.visit == Visit::Active)
    return node.module.state() == ModuleV2::State::Active;
  if (node.visit == Visit::Visiting) return false;  // Dependency cycle.
  node.visit = Visit::Visiting;
  risc_provider_dependency_v1 deps[kMaxModules]{};
  for (size_t i = 0; i < node.spec.requirementCount; ++i) {
    const RequirementV2& requirement = node.spec.requirements[i];
    int indexOfDependency = find(requirement.capability, requirement.api);
    if (indexOfDependency < 0 ||
        !activate(static_cast<size_t>(indexOfDependency)) ||
        !nodes_[indexOfDependency].module.pinConsumer()) {
      releaseDependencies(index);
      node.visit = Visit::Idle;
      return false;
    }
    node.dependencies[node.acquired++] = static_cast<uint8_t>(indexOfDependency);
    deps[i] = {requirement.capability, requirement.api,
               nodes_[indexOfDependency].module.capability()};
  }
  if (!node.module.load(node.spec.verifiedElfPath, node.spec.id,
                        node.spec.provides, node.spec.api,
                        node.spec.requirementCount ? deps : nullptr,
                        node.spec.requirementCount)) {
    // The loader can retain Failed state after a clean dlclose. Reset that
    // state before rollback so a later activation can retry safely.
    (void)node.module.unload();
    releaseDependencies(index);
    node.visit = Visit::Idle;
    return false;
  }
  node.visit = Visit::Active;
  return true;
}

GrantV2 GraphV2::acquire(const char* capability, uint32_t api) {
  int target = find(capability, api);
  if (target < 0) return {};
  size_t slot = kMaxGrants;
  for (size_t i = 0; i < kMaxGrants; ++i)
    if (!grants_[i].occupied) { slot = i; break; }
  if (slot == kMaxGrants || !activate(static_cast<size_t>(target))) return {};
  if (!nodes_[target].module.pinConsumer()) {
    (void)deactivateIfUnused(static_cast<size_t>(target));
    return {};
  }
  ++nextGeneration_;
  if (!nextGeneration_) ++nextGeneration_;
  grants_[slot] = {nextGeneration_, static_cast<uint8_t>(target), true};
  return {static_cast<uint32_t>(slot + 1), nextGeneration_};
}

const void* GraphV2::interfaceFor(GrantV2 grant) const {
  if (!grant.slot || grant.slot > kMaxGrants || !grant.generation) return nullptr;
  const GrantSlot& slot = grants_[grant.slot - 1];
  if (!slot.occupied || slot.generation != grant.generation) return nullptr;
  return nodes_[slot.node].module.capability();
}

bool GraphV2::release(GrantV2 grant) {
  if (!interfaceFor(grant)) return false;
  GrantSlot& slot = grants_[grant.slot - 1];
  const size_t node = slot.node;
  slot.occupied = false;
  return nodes_[node].module.unpinConsumer() && deactivateIfUnused(node);
}

size_t GraphV2::liveGrants() const {
  size_t total = 0;
  for (const GrantSlot& grant : grants_) if (grant.occupied) ++total;
  return total;
}

bool GraphV2::shutdown() {
  if (liveGrants()) return false;
  for (size_t pass = 0; pass < count_; ++pass)
    for (size_t i = 0; i < count_; ++i)
      if (nodes_[i].visit == Visit::Active &&
          !nodes_[i].module.consumers() && !deactivateIfUnused(i)) return false;
  for (size_t i = 0; i < count_; ++i) {
    if (nodes_[i].visit != Visit::Idle) return false;
    if (nodes_[i].module.state() == ModuleV2::State::Failed &&
        !nodes_[i].module.unload()) return false;
    if (nodes_[i].module.state() != ModuleV2::State::Absent) return false;
  }
  return true;
}
}  // namespace RuntimeProviders
