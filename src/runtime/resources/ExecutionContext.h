#pragma once

#include <cstddef>
#include <cstdint>

// A bounded, firmware-owned lifetime for one native ELF invocation. Resource
// destructors run on the owning app task BEFORE dlclose; none may retain an
// ELF function pointer. Registration, termination and callback invocation are
// owner-task operations. Workers must use their own synchronization.
namespace RuntimeResources {
class ExecutionContext final {
 public:
  enum class State : uint8_t { Terminated, Running, Stopping };
  enum class Resource : uint8_t {
    Streams = 1, SerialPort = 2, Programmer = 3, DeviceLeases = 4,
    DeviceEvents = 5, Dependencies = 6, GnssDriver = 7
  };
  using Cleanup = void (*)(void* opaque, uint32_t invocation);
  using Stop = void (*)(void* opaque, uint32_t invocation);
  static constexpr size_t kMaxResources = 8;

  static ExecutionContext* current() { return activeSlot(); }

  // Owner-task allocation shared by app and installed-provider lifetimes.
  // IDs never wrap or alias, including contexts with no foreground app.
  static uint32_t reserveIdentity() {
    return nextId_ == UINT32_MAX ? 0 : ++nextId_;
  }
  bool begin() {
    if (activeSlot() || state_ != State::Terminated || ending_ || notifying_ ||
        nextId_ == UINT32_MAX) return false;
    id_ = reserveIdentity();
    count_ = 0;
    state_ = State::Running;
    activeSlot() = this;
    return true;
  }
  uint32_t id() const { return id_; }
  State state() const { return state_; }
  bool running(uint32_t invocation) const {
    return state_ == State::Running && invocation && invocation == id_;
  }

  bool track(Resource kind, Cleanup cleanup, void* opaque = nullptr, Stop stop = nullptr) {
    if (state_ != State::Running || !cleanup || count_ == kMaxResources) return false;
    for (size_t i = 0; i < count_; ++i)
      if (entries_[i].kind == kind) return false;
    entries_[count_++] = {kind, cleanup, opaque, stop};
    return true;
  }

  bool untrack(Resource kind, uint32_t invocation) {
    if (!invocation || invocation != id_ || state_ == State::Terminated ||
        ending_ || notifying_) return false;
    for (size_t i = 0; i < count_; ++i) {
      if (entries_[i].kind != kind) continue;
      for (size_t j = i + 1; j < count_; ++j) entries_[j - 1] = entries_[j];
      entries_[--count_] = {};
      return true;
    }
    return false;
  }

  void requestStop() {
    if (state_ != State::Running) return;
    state_ = State::Stopping;
    notifying_ = true;
    for (size_t i = count_; i > 0; --i) {
      const Entry& entry = entries_[i - 1];
      if (entry.stop) entry.stop(entry.opaque, id_);
    }
    notifying_ = false;
  }

  void end() {
    if (state_ == State::Terminated || ending_ || notifying_) return;
    requestStop();
    for (size_t i = 0; i < count_; ++i)
      if (entries_[i].kind == Resource::Programmer) return;
    ending_ = true;
    const uint32_t invocation = id_;
    while (count_) {
      const Entry entry = entries_[--count_];
      entries_[count_] = {};
      entry.cleanup(entry.opaque, invocation);
    }
    id_ = 0;
    state_ = State::Terminated;
    if (activeSlot() == this) activeSlot() = nullptr;
    ending_ = false;
  }

 private:
  static ExecutionContext*& activeSlot() {
    static ExecutionContext* currentContext = nullptr;
    return currentContext;
  }
  struct Entry {
    Resource kind = Resource::Streams;
    Cleanup cleanup = nullptr;
    void* opaque = nullptr;
    Stop stop = nullptr;
  };
  Entry entries_[kMaxResources]{};
  inline static uint32_t nextId_ = 0;
  uint32_t id_ = 0;
  size_t count_ = 0;
  State state_ = State::Terminated;
  bool ending_ = false;
  bool notifying_ = false;
};
}  // namespace RuntimeResources
