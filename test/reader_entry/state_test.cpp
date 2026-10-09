#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "native/NativeReaderEntryState.h"

using NativeReaderEntry::Action;
using NativeReaderEntry::State;

int main() {
  State state;
  int activity = 0, other = 0;
  assert(!state.mapped() && !state.pending() && !state.failed());
  assert(!state.request(Action::ActivityLoop, &activity));
  assert(state.take().action == Action::None);
  for (unsigned i = 0; i < 1000; ++i) {
    assert(state.begin());
    assert(!state.begin());
    assert(!state.request(Action::None));
    assert(state.request(Action::ActivityLoop, &activity));
    assert(state.request(Action::ActivityLoop, &activity));
    assert(!state.request(Action::ActivityLoop, &other));
    assert(!state.request(Action::Sleep));
    assert(state.pending());
    assert(state.take().action == Action::None); // Cannot dispatch a mapped app.
    assert(state.pending());
    state.end(true);
    assert(!state.begin()); // Pending work must be consumed before reload.
    const auto next = state.take();
    assert(next.action == Action::ActivityLoop && next.activity == &activity);
    assert(state.take().action == Action::None); // Exactly once.
  }
  for (const auto action : {Action::Sleep, Action::SleepKeepingScreen, Action::PowerOff}) {
    assert(state.begin());
    assert(state.request(action, nullptr, false));
    assert(!state.request(action, nullptr, true));
    state.end(true);
    const auto next = state.take();
    assert(next.action == action && !next.activity && !next.wakeOnTouch);
  }
  assert(state.begin());
  assert(state.request(Action::ActivityLoop, &activity));
  state.end(false); // Failed unload must discard pending work and block reload.
  assert(state.failed() && !state.mapped() && !state.pending());
  assert(state.take().action == Action::None);
  assert(!state.begin());
  state.end(true); // A later nominal completion cannot erase a retained failure.
  assert(state.failed() && !state.begin());
  std::puts("Reader entry state: ordering, duplicate/conflicting requests, sleep, and retained failure PASS");
}
