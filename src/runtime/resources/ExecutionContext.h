#pragma once

#include <cstddef>
#include <cstdint>

// A bounded, firmware-owned lifetime for one native ELF invocation. Resource
// destructors run on the owning app task BEFORE dlclose; none may retain an
// ELF function pointer. This class allocates no heap and never reuses an ID.
// Registration, teardown and callbacks are owner-task operations; worker tasks
// must use the stream registry's own synchronization and owner checks.
namespace RuntimeResources {
class ExecutionContext final {
 public:
  enum class State : uint8_t { Terminated, Running, Stopping };
  enum class Resource : uint8_t { Streams = 1, SerialPort = 2, Programmer = 3 };
  using Cleanup = void (*)(void* opaque, uint32_t invocation);
  static constexpr size_t kMaxResources = 8;

  bool begin() {
    if (state_ != State::Terminated || nextId_ == UINT32_MAX) return false;
    id_ = ++nextId_;
    count_ = 0;
    state_ = State::Running;
    return true;
  }

  uint32_t id() const { return id_; }
  State state() const { return state_; }
  bool running(uint32_t invocation) const {
    return state_ == State::Running && invocation && invocation == id_;
  }

  bool track(Resource kind, Cleanup cleanup, void* opaque = nullptr) {
    if (state_ != State::Running || !cleanup || count_ == kMaxResources) return false;
    for (size_t i = 0; i < count_; ++i) {
      if (entries_[i].kind == kind) return false;
    }
    entries_[count_++] = {kind, cleanup, opaque};
    return true;
  }

  // Blocks new app-facing acquisitions; registered resources remain available
  // to their firmware cleanup routines until end() finishes.
  void requestStop() {
    if (state_ == State::Running) state_ = State::Stopping;
  }

  void end() {
    if (state_ == State::Terminated || ending_) return;
    requestStop();
    ending_ = true;
    const uint32_t invocation = id_;
    // LIFO: dependent serial leases close before the underlying stream table.
    // Decrement first so a reentrant end() cannot destroy the same resource.
    while (count_) {
      const Entry entry = entries_[--count_];
      entries_[count_] = {};
      entry.cleanup(entry.opaque, invocation);
    }
    id_ = 0;
    state_ = State::Terminated;
    ending_ = false;
  }

 private:
  struct Entry {
    Resource kind = Resource::Streams;
    Cleanup cleanup = nullptr;
    void* opaque = nullptr;
  };
  Entry entries_[kMaxResources]{};
  uint32_t nextId_ = 0;
  uint32_t id_ = 0;
  size_t count_ = 0;
  State state_ = State::Terminated;
  bool ending_ = false;
};
}  // namespace RuntimeResources
