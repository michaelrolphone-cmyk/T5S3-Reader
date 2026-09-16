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
  f.context->end();
}
static void serialCleanup(void* opaque, uint32_t id) {
  auto& f = *static_cast<Fixture*>(opaque);
  assert(f.invocation == id);
  f.order[f.calls++] = 2;
}
static void programmerCleanup(void* opaque, uint32_t id) {
  auto& f = *static_cast<Fixture*>(opaque);
  assert(f.invocation == id);
  // This callback must never destroy an in-flight synchronous operation.
  assert(false && "An active programmer must untrack before teardown");
}
static void programmerStop(void* opaque, uint32_t id) {
  auto& f = *static_cast<Fixture*>(opaque);
  assert(f.invocation == id && f.context->state() == ExecutionContext::State::Stopping);
  assert(f.calls == 0);
  ++f.stopCalls;
  f.context->requestStop();
  f.context->end();  // Reentrant end defers ALL dependent resource cleanup.
  assert(f.calls == 0);
}
int main() {
  ExecutionContext context;
  ExecutionContext another;
  Fixture fixture{&context};
  assert(!ExecutionContext::current());
  context.end();
  assert(context.begin());
  const uint32_t first = context.id();
  fixture.invocation = first;
  assert(ExecutionContext::current() == &context);
  assert(!context.begin() && !another.begin());
  assert(context.track(ExecutionContext::Resource::Streams, streamCleanup, &fixture));
  assert(context.track(ExecutionContext::Resource::SerialPort, serialCleanup, &fixture));
  assert(!context.track(ExecutionContext::Resource::SerialPort, serialCleanup, &fixture));
  assert(!context.track(ExecutionContext::Resource::Programmer, nullptr, &fixture));
  assert(context.track(ExecutionContext::Resource::Programmer, programmerCleanup, &fixture,
                       programmerStop));
  assert(!context.untrack(ExecutionContext::Resource::Programmer, first + 1));
  assert(!context.untrack(ExecutionContext::Resource::Streams, 0));
  assert(context.untrack(ExecutionContext::Resource::Programmer, first));
  assert(!context.untrack(ExecutionContext::Resource::Programmer, first));
  assert(context.track(ExecutionContext::Resource::Programmer, programmerCleanup, &fixture,
                       programmerStop));
  context.requestStop();
  context.end();
  assert(fixture.stopCalls == 1 && fixture.calls == 0);
  assert(context.state() == ExecutionContext::State::Stopping);
  assert(ExecutionContext::current() == &context && !another.begin());
  assert(!context.running(first));
  assert(!context.track(ExecutionContext::Resource::Programmer, programmerCleanup, &fixture));
  assert(context.untrack(ExecutionContext::Resource::Programmer, first));
  context.end();
  context.end();
  assert(fixture.calls == 2 && fixture.order[0] == 2 && fixture.order[1] == 1);
  assert(context.id() == 0 && context.state() == ExecutionContext::State::Terminated);
  assert(!ExecutionContext::current());
  assert(!context.untrack(ExecutionContext::Resource::Streams, first));

  // An end request cannot free a programmer's input stream or lease while its
  // synchronous stack is active. Completion/lease release then untracks it.
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
  assert(fixture.stopCalls == 1 && fixture.calls == 0);
  assert(context.state() == ExecutionContext::State::Stopping);
  assert(context.untrack(ExecutionContext::Resource::Programmer, second));
  context.end();
  assert(fixture.calls == 2 && fixture.order[0] == 2 && fixture.order[1] == 1);
  assert(!ExecutionContext::current());
  assert(context.begin());
  context.requestStop();
  context.end();
  assert(!ExecutionContext::current());
  assert(another.begin());
  another.end();
  std::puts("Native execution-context owner and deferred programmer teardown tests passed");
}
