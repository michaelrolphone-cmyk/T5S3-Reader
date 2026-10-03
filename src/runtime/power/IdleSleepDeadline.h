#pragma once
#include <cstdint>

// The single owner-task idle deadline shared by firmware UI and synchronous
// native app input. Expiry is latched until the app has cooperatively unwound;
// cleanup input and ordinary app-return bookkeeping cannot erase it.
class IdleSleepDeadline {
 public:
  bool observe(uint32_t now, uint32_t timeout, bool activity, bool inhibited) {
    if (pending_) return true;
    if (!initialized_ || activity || inhibited) reset(now);
    if (!inhibited && static_cast<uint32_t>(now - lastActivity_) >= timeout) pending_ = true;
    return pending_;
  }
  void activity(uint32_t now) { if (!pending_) reset(now); }
  bool pending() const { return pending_; }
  uint32_t inactiveFor(uint32_t now) const { return initialized_ ? static_cast<uint32_t>(now - lastActivity_) : 0u; }
  bool consume(uint32_t now) {
    if (!pending_) return false;
    reset(now);
    return true;
  }
 private:
  void reset(uint32_t now) { lastActivity_ = now; initialized_ = true; pending_ = false; }
  uint32_t lastActivity_ = 0;
  bool initialized_ = false, pending_ = false;
};
