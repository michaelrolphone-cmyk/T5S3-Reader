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
  assert(module.state() == RuntimeProviders::ModuleV2::State::Active);
  assert(module.capability() == interface);
  assert(module.pinConsumer() && module.unpinConsumer());
  assert(!module.unload());
  // The fixture refuses quiescence permanently: intentionally leave mapped.
  std::puts("Generic provider refuses unsafe unmap when quiesce fails: PASS");
  return 0;
}
