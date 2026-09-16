#include "runtime/resources/ExecutionContext.h"
#include <cassert>
#include <cstdio>

using RuntimeResources::ExecutionContext;

struct Fixture {
  ExecutionContext* context;
  unsigned calls = 0;
  int order[3]{};
  uint32_t invocation = 0;
};

static void streamCleanup(void* opaque, uint32_t id) {
  auto& f = *static_cast<Fixture*>(opaque);
  assert(f.context->state() == ExecutionContext::State::Stopping);
  assert(!f.context->running(id));
  assert(f.invocation == id);
  f.order[f.calls++] = 1;
  f.context->end();  // Reentrant termination cannot double-release.
}
static void serialCleanup(void* opaque, uint32_t id) {
  auto& f = *static_cast<Fixture*>(opaque);
  assert(f.invocation == id);
  f.order[f.calls++] = 2;
}

int main() {
  ExecutionContext context;
  Fixture fixture{&context};
  assert(context.state() == ExecutionContext::State::Terminated);
  assert(!context.running(0));
  context.end();  // Partial/duplicate shutdown is harmless.
  assert(context.begin());
  const uint32_t first = context.id();
  fixture.invocation = first;
  assert(first && context.running(first));
  assert(!context.begin());  // Nested app launch is not another context.
  assert(context.track(ExecutionContext::Resource::Streams, streamCleanup, &fixture));
  assert(context.track(ExecutionContext::Resource::SerialPort, serialCleanup, &fixture));
  assert(!context.track(ExecutionContext::Resource::SerialPort, serialCleanup, &fixture));
  assert(!context.track(ExecutionContext::Resource::Programmer, nullptr, &fixture));
  context.requestStop();
  context.requestStop();
  assert(!context.running(first));
  assert(!context.track(ExecutionContext::Resource::Programmer, serialCleanup, &fixture));
  context.end();
  context.end();
  assert(fixture.calls == 2 && fixture.order[0] == 2 && fixture.order[1] == 1);
  assert(context.id() == 0 && context.state() == ExecutionContext::State::Terminated);

  assert(context.begin());
  const uint32_t second = context.id();
  assert(second != first && !context.running(first) && context.running(second));
  context.end(); // No registered resources is safe.
  assert(context.begin());
  context.requestStop();
  context.end(); // Partial startup is safe.
  std::puts("Native execution-context ownership tests passed");
}
