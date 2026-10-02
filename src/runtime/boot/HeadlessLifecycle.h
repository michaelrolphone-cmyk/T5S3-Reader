#pragma once
#include <cstddef>
#include <cstdint>

namespace RuntimeBoot {
enum class State : uint8_t { Cold, Mount, Recover, Inventory, Prepare, Bind, Running, Idle, Stopping, Retained };
enum class Result : uint8_t { Ready, Absent, Fault };

// A boot coordinator, not a second scheduler or provider registry. Adapter
// methods delegate to the ordinary package engine and installed graph. One
// bounded operation per tick; fault/absence never starts an automatic retry.
template<class Adapter> class HeadlessLifecycle {
 public:
  explicit HeadlessLifecycle(Adapter& adapter) : adapter_(adapter) {}
  State state() const { return state_; }
  void start() { if (state_ == State::Cold) state_ = State::Mount; }
  void stop() {
    if (state_ != State::Cold && state_ != State::Retained) state_ = State::Stopping;
  }
  void tick() {
    switch (state_) {
      case State::Mount:
        transition(adapter_.mount(), State::Recover); break;
      case State::Recover:
        if (index_ == adapter_.count()) { index_ = 0; state_ = State::Inventory; break; }
        transition(adapter_.recover(index_++), State::Recover); break;
      case State::Inventory:
        transition(adapter_.inventory(), State::Prepare); break;
      case State::Prepare:
        transition(adapter_.prepare(), State::Bind); break;
      case State::Bind:
        if (index_ == adapter_.count()) { state_ = State::Running; break; }
        transition(adapter_.bind(index_++), State::Bind); break;
      case State::Running: adapter_.poll(); break;
      case State::Stopping:
        // A failed release/shutdown retains the mount and all mappings.
        if (!adapter_.shutdown()) { state_ = State::Retained; break; }
        if (!adapter_.unmount()) { state_ = State::Retained; break; }
        index_ = 0; state_ = State::Cold; break;
      case State::Idle: case State::Retained:
        adapter_.poll(); break;
      case State::Cold: break;
    }
  }
 private:
  void transition(Result result, State next) {
    if (result == Result::Ready) { state_ = next; return; }
    // Roll back earlier grants before idle; no unmap/unmount on uncertainty.
    state_ = adapter_.shutdown() ? State::Idle : State::Retained;
  }
  Adapter& adapter_;
  State state_ = State::Cold;
  size_t index_ = 0;
};
} // namespace RuntimeBoot
