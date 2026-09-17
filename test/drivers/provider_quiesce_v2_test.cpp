#include "runtime/drivers/ProviderModuleV2.h"
#include <cassert>
#include <cstdio>

int main(int argc, char** argv) {
  assert(argc == 2);
  RuntimeProviders::ModuleV2 module;
  assert(module.load(argv[1], "fixture-stuck", "cap.stuck", 1, nullptr, 0));
  const void* interface = module.capability();
  assert(interface && module.consumers() == 0);
  // Hardware ownership is not equivalent to the software consumer count.
  assert(!module.unload());
  // Quiesce can have partially freed a resource: the provider must remain
  // mapped but MUST NOT be regranted or expose its capability after failure.
  assert(module.state() == RuntimeProviders::ModuleV2::State::Failed);
  assert(!module.capability() && !module.pinConsumer());
  assert(!module.unpinConsumer());
  assert(!module.unload());
  assert(module.state() == RuntimeProviders::ModuleV2::State::Failed);
  // This fixture refuses quiescence permanently: intentionally leave mapped.
  std::puts("Generic provider quarantines unsafe teardown and rejects regrant: PASS");
  return 0;
}
