#include "runtime/drivers/ProviderGraphV2.h"
#include <cassert>
#include <cstdio>
using namespace RuntimeProviders;
int main(int argc, char** argv) {
  assert(argc == 4);
  GraphV2 graph;
  const RequirementV2 needs[]={{"cap.retry",1}};
  assert(graph.addVerified({"fixture-root",argv[1],"cap.root",1,nullptr,0}));
  assert(graph.addVerified({"fixture-retry",argv[2],"cap.retry",1,nullptr,0}));
  assert(graph.addVerified({"fixture-child",argv[3],"cap.child",1,needs,1}));
  auto persistent=graph.acquire("cap.root",1);
  auto child=graph.acquire("cap.child",1);
  assert(persistent.slot && child.slot);
  assert(!graph.drainExcept(&persistent,1));
  assert(graph.release(child)); // Top-level release succeeds; lower teardown fails.
  assert(!graph.drainExcept(&persistent,1)); // Retry still cannot prove safe teardown.
  assert(graph.interfaceFor(persistent));
  assert(graph.drainExcept(&persistent,1)); // Later exact cleanup succeeds.
  assert(!graph.interfaceFor(child));
  auto wake=graph.acquire("cap.child",1);
  assert(wake.slot && wake.generation!=child.generation);
  assert(!graph.interfaceFor(child));
  assert(graph.release(wake));
  assert(!graph.drainExcept(&persistent,1));
  assert(graph.drainExcept(&persistent,1));
  assert(graph.release(persistent));
  assert(graph.shutdown());
  std::puts("Persistent lease boundary: partial dependency failure, retry and wake generation PASS");
}
