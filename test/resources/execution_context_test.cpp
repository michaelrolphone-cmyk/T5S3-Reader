#include "runtime/resources/ExecutionContext.h"
#include <cassert>
#include <cstdio>

using RuntimeResources::ExecutionContext;

struct Fixture {
  ExecutionContext* context;
  unsigned calls = 0;
  int order[4]{};
  unsigned stopCalls = 0;
  uint32_t invocation = 0;
};

static void streamCleanup(void* opaque, uint32_t id) {
  auto& f = *static_cast<Fixture*>(opaque);
  assert(f.context->state() == ExecutionContext::State::Stopping);
  assert(!f.context->running(id) && f.invocation == id);
  f.order[f.calls++] = 1;
  f.context->end();  // Reentrant termination cannot double-release.
}
static void serialCleanup(void* opaque, uint32_t id) {
  auto& f = *static_cast<Fixture*>(opaque);
  assert(f.invocation == id);
  f.order[f.calls++] = 2;
}
static void programmerCleanup(void* opaque, uint32_t id) {
  auto& f = *static_cast<Fixture*>(opaque);
  assert(f.invocation == id && f.context->state() == ExecutionContext::State::Stopping);
  assert(f.stopCalls == 1);  // Stop notification precedes dependent destruction.
  f.order[f.calls++] = 3;
}
static void programmerStop(void* opaque, uint32_t id) {
  auto& f = *static_cast<Fixture*>(opaque);
  assert(f.invocation == id && f.context->state() == ExecutionContext::State::Stopping);
  assert(f.calls == 0);  // Leases and streams are still intact for cancellation.
  ++f.stopCalls;
  f.context->requestStop();
  f.context->end();  // A stop callback cannot trigger premature destruction.
  assert(f.calls == 0);
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

  // A completed synchronous job unregisters before its operation memory is
  // freed; only the exact invocation can remove it, and its slot can be reused.
  assert(context.track(ExecutionContext::Resource::Programmer, programmerCleanup, &fixture,
                       programmerStop));
  assert(!context.untrack(ExecutionContext::Resource::Programmer, first + 1));
  assert(!context.untrack(ExecutionContext::Resource::Streams, 0));
  assert(context.untrack(ExecutionContext::Resource::Programmer, first));
  assert(!context.untrack(ExecutionContext::Resource::Programmer, first));
  assert(context.track(ExecutionContext::Resource::Programmer, programmerCleanup, &fixture,
                       programmerStop));

  context.requestStop();
  context.requestStop();
  assert(fixture.stopCalls == 1 && fixture.calls == 0);
  assert(!context.running(first));
  assert(!context.track(ExecutionContext::Resource::Programmer, programmerCleanup, &fixture));
  // A cooperative job may finish after stop notification. It must unregister
  // before its state is freed so teardown cannot call a dangling destructor.
  assert(context.untrack(ExecutionContext::Resource::Programmer, first));
  context.end();
  context.end();
  assert(fixture.calls == 2 && fixture.order[0] == 2 && fixture.order[1] == 1);
  assert(context.id() == 0 && context.state() == ExecutionContext::State::Terminated);
  assert(!context.untrack(ExecutionContext::Resource::Streams, first));

  // A job still registered when end() starts is signaled before its own
  // destructor and before the serial/stream resources it consumes are closed.
  assert(context.begin());
  const uint32_t second = context.id();
  assert(second != first && !context.running(first) && context.running(second));
  fixture = Fixture{&context};
  fixture.invocation = second;
  assert(context.track(ExecutionContext::Resource::Streams, streamCleanup, &fixture));
  assert(context.track(ExecutionContext::Resource::SerialPort, serialCleanup, &fixture));
  assert(context.track(ExecutionContext::Resource::Programmer, programmerCleanup, &fixture,
                       programmerStop));
  assert(!context.untrack(ExecutionContext::Resource::Programmer, first));
  context.end();
  assert(fixture.stopCalls == 1 && fixture.calls == 3);
  assert(fixture.order[0] == 3 && fixture.order[1] == 2 && fixture.order[2] == 1);
  context.end();

  assert(context.begin());
  context.requestStop();
  context.end();  // Partial startup is safe.
  std::puts("Native execution-context ownership and cooperative-stop tests passed");
}
