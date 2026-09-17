#include "runtime/drivers/ProviderGraphV2.h"
#include <cassert>
#include <cstdio>

using RuntimeProviders::GraphV2;
using RuntimeProviders::RequirementV2;
using RuntimeProviders::SpecV2;

int main(int argc, char** argv) {
  assert(argc == 4);
  const RequirementV2 needsRoot[] = {{"cap.root", 1}};
  const SpecV2 root = {"fixture-root", argv[1], "cap.root", 1, nullptr, 0};
  const SpecV2 child = {"fixture-child", argv[2], "cap.child", 1, needsRoot, 1};
  const SpecV2 other = {"fixture-other", argv[3], "cap.other", 1, needsRoot, 1};

  GraphV2 graph;
  assert(graph.addVerified(root) && graph.addVerified(child) && graph.addVerified(other));
  assert(graph.moduleCount() == 3);
  assert(!graph.addVerified(root));
  assert(!graph.acquire("cap.unknown", 1).slot);
  assert(!graph.acquire("cap.child", 2).slot);
  auto childGrant = graph.acquire("cap.child", 1);
  assert(childGrant.slot && graph.interfaceFor(childGrant));
  assert(*static_cast<const int*>(graph.interfaceFor(childGrant)) == 42);
  assert(!graph.addVerified({"extra", argv[1], "cap.extra", 1, nullptr, 0}));
  auto otherGrant = graph.acquire("cap.other", 1);
  assert(otherGrant.slot && graph.liveGrants() == 2);
  assert(!graph.shutdown());
  assert(graph.release(childGrant));
  assert(!graph.release(childGrant) && !graph.interfaceFor(childGrant));
  auto replacement = graph.acquire("cap.child", 1);
  assert(replacement.slot && replacement.generation != childGrant.generation);
  assert(!graph.interfaceFor(childGrant));
  assert(graph.release(otherGrant) && graph.interfaceFor(replacement));
  assert(graph.release(replacement) && graph.shutdown());
  assert(graph.liveGrants() == 0);
  // Unloading after all dependent grants permits a clean new activation.
  auto again = graph.acquire("cap.child", 1);
  assert(again.slot && graph.release(again) && graph.shutdown());

  // The same graph also supports pure provider capabilities with no USB names.
  GraphV2 capacity;
  assert(capacity.addVerified(root));
  RuntimeProviders::GrantV2 grants[GraphV2::kMaxGrants];
  for (size_t i = 0; i < GraphV2::kMaxGrants; ++i) {
    grants[i] = capacity.acquire("cap.root", 1);
    assert(grants[i].slot);
  }
  assert(!capacity.acquire("cap.root", 1).slot);
  for (auto grant : grants) assert(capacity.release(grant));
  assert(capacity.shutdown());

  GraphV2 missing;
  assert(missing.addVerified(child));
  assert(!missing.acquire("cap.child", 1).slot && missing.shutdown());

  const RequirementV2 needsB[] = {{"cycle.b", 1}};
  const RequirementV2 needsA[] = {{"cycle.a", 1}};
  GraphV2 cycle;
  // Neither ELF is opened: the manifest cycle is rejected first.
  assert(cycle.addVerified({"cycle-a", argv[1], "cycle.a", 1, needsB, 1}));
  assert(cycle.addVerified({"cycle-b", argv[2], "cycle.b", 1, needsA, 1}));
  assert(!cycle.acquire("cycle.a", 1).slot && cycle.shutdown());

  GraphV2 mismatch;
  assert(mismatch.addVerified({"not-the-ELF-id", argv[1], "cap.root", 1, nullptr, 0}));
  assert(!mismatch.acquire("cap.root", 1).slot);
  assert(mismatch.shutdown()); // Failed identity validation must close the DSO.

  const RequirementV2 duplicateRequirements[] = {{"cap.root", 1}, {"cap.root", 2}};
  GraphV2 invalid;
  assert(!invalid.addVerified({"invalid", argv[1], "cap.invalid", 1,
                               duplicateRequirements, 2}));
  assert(!invalid.addVerified({"invalid", "relative/path", "cap.invalid", 1,
                               nullptr, 0}));
  std::puts("Generic graph: dependencies, cycle detection, rollback, shared lifetime and grants PASS");
}
