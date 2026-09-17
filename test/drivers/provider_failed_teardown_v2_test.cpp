#include "runtime/drivers/ProviderGraphV2.h"
#include <cassert>
#include <cstdio>

int main(int argc, char **argv) {
  assert(argc == 2);
  RuntimeProviders::GraphV2 graph;
  assert(graph.addVerified({"fixture-retry", argv[1], "cap.retry", 1,
                            nullptr, 0}));
  const auto grant = graph.acquire("cap.retry", 1);
  assert(grant.slot && graph.interfaceFor(grant));
  // First quiesce fails after all client grants are released. Its internal
  // hardware state may now be partially torn down, not safe for another call.
  assert(!graph.release(grant));
  assert(!graph.interfaceFor(grant) && graph.liveGrants() == 0);
  assert(!graph.acquire("cap.retry", 1).slot); // No regrant while quarantined.
  assert(!graph.addVerified({"unsafe", argv[1], "cap.other", 1, nullptr, 0}));
  assert(graph.shutdown()); // Second quiesce succeeds; unload only NOW.
  assert(graph.shutdown());
  const auto again = graph.acquire("cap.retry", 1);
  assert(again.slot && again.generation != grant.generation);
  assert(!graph.interfaceFor(grant));
  // Unloading and reloading the test ELF resets its fail-once state.
  assert(!graph.release(again));
  assert(graph.shutdown());
  std::puts("Active teardown quarantine: no regrant, recovery, fresh generation PASS");
}
