// The production programmer now requires a live firmware-owned app invocation.
// Keep the existing protocol/fault test cases unchanged by owning one context
// for that process; separate lifecycle tests explicitly manage stop/exit.
#include "runtime/resources/ExecutionContext.h"
#include <cstdlib>

namespace {
RuntimeResources::ExecutionContext context;
struct Bootstrap {
  Bootstrap() { if (!context.begin()) std::abort(); }
  ~Bootstrap() { context.end(); }
} bootstrap;
}  // namespace
