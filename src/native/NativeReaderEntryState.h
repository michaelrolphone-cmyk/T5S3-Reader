#pragma once
#include <cstdint>

// One owner task. All pending work is firmware-owned, never an ELF callback,
// return address, allocation or borrowed path. No action can run while mapped.
namespace NativeReaderEntry {
enum class Action : uint8_t { None, ActivityLoop, Sleep, SleepKeepingScreen, PowerOff };
struct Pending {
  Action action = Action::None;
  void* activity = nullptr;
  bool wakeOnTouch = true;
};
class State {
 public:
  bool begin() {
    if (mapped_ || pending_.action != Action::None || failed_) return false;
    mapped_ = true;
    return true;
  }
  bool request(Action action, void* activity = nullptr, bool wakeOnTouch = true) {
    if (!mapped_ || action == Action::None) return false;
    if (pending_.action != Action::None)
      return pending_.action == action && pending_.activity == activity &&
             pending_.wakeOnTouch == wakeOnTouch;
    pending_ = {action, activity, wakeOnTouch};
    return true;
  }
  void end(bool okay) {
    mapped_ = false;
    if (!okay) { pending_ = {}; failed_ = true; }
  }
  bool mapped() const { return mapped_; }
  bool pending() const { return pending_.action != Action::None; }
  bool failed() const { return failed_; }
  Pending take() {
    if (mapped_ || failed_) return {};
    const Pending result = pending_;
    pending_ = {};
    return result;
  }
 private:
  bool mapped_ = false;
  bool failed_ = false;
  Pending pending_{};
};
}  // namespace NativeReaderEntry
