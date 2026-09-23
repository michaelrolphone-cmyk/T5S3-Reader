#pragma once
#include <cstdint>
#include "RiscUsbVbusV1.h"

// Executed only by the existing owner-task poll. No worker, app pointers,
// package reloads or input queues. Port owns every physical side effect.
class UsbRoleSwitch {
 public:
  enum class State { Off, Sense, Host, Failed };
  void begin(uint32_t now) {
    state_ = State::Sense; since_ = now;
    absent_ = external_ = false; readFailures_ = startFailures_ = 0;
    error_ = nullptr;
  }
  void stop() { state_ = State::Off; }
  State state() const { return state_; }
  const char *diagnostic() const {
    if (state_ == State::Failed) return error_;
    if (state_ == State::Sense)
      return external_ ? "EXTERNAL POWER; CHARGING / USB SERIAL" : "SOURCE OFF; CHECKING USB INPUT POWER";
    return "USB ROLE OFF";
  }
  template <class Port> void poll(Port &port, uint32_t now) {
    if (state_ == State::Off || state_ == State::Failed) return;
    if (state_ == State::Host) {
      // Include the root port's physical attach bit, queued events and claims,
      // not just the published device count: enumeration may be in progress.
      if (port.busy()) { since_ = now; return; }
      if (static_cast<uint32_t>(now - since_) < 2000) return;
      if (!port.park()) { fail(port, "ROLE: HOST CLEANUP FAILED; RESOURCES RETAINED"); return; }
      state_ = State::Sense; since_ = port.now(); absent_ = external_ = false;
      port.report(diagnostic());
      return;
    }
    // BQ input qualification needs 220 ms after source-off. Two clean samples
    // 500 ms apart also debounce unplug before any host/PHY takeover. Polls
    // return between samples so the UI and serial console remain responsive.
    const uint32_t wait = startFailures_ ? (1000u << startFailures_) : 500u;
    if (static_cast<uint32_t>(now - since_) < wait) return;
    since_ = now;
    const int32_t input = port.input();
    if (input == RISC_USB_POWER_EXTERNAL) {
      if (!external_) { external_ = true; port.report(diagnostic()); }
      absent_ = false; readFailures_ = startFailures_ = 0;
      return; // Never probe the host or touch the PHY on external power.
    }
    if (input != RISC_USB_POWER_ABSENT) {
      absent_ = false;
      if (++readFailures_ >= 3) fail(port, "ROLE: INPUT POWER UNKNOWN; HOST BLOCKED");
      return; // Unknown is not evidence that it is safe to source VBUS.
    }
    readFailures_ = 0; external_ = false;
    if (!absent_) { absent_ = true; return; }
    if (port.start()) {
      state_ = State::Host; since_ = port.now(); startFailures_ = 0;
      port.report("USB HOST; CONTROLLER DISCOVERY");
      return;
    }
    // A failed startup can still own DMA, PHY or a partial power lease.
    // Prove cleanup before restoring serial or attempting anything else.
    if (!port.park()) { fail(port, "ROLE: HOST CLEANUP FAILED; RESOURCES RETAINED"); return; }
    if (++startFailures_ >= 3) { fail(port, "ROLE: HOST START FAILED THREE TIMES"); return; }
    since_ = port.now(); absent_ = false;
    port.report("HOST START FAILED; BOUNDED RETRY");
  }
 private:
  template <class Port> void fail(Port &port, const char *reason) {
    state_ = State::Failed;
    error_ = reason;
    port.report(diagnostic());
  }
  State state_ = State::Off;
  uint32_t since_ = 0;
  uint8_t readFailures_ = 0, startFailures_ = 0;
  bool absent_ = false, external_ = false;
  const char *error_ = nullptr;
};
