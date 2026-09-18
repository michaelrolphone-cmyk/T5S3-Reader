#pragma once

// Pure firmware-internal policy for the trusted permission screen. The prompt
// must first observe a completely idle touch frame after it is displayed.
// This consumes an app-originated held touch or pending tap without allowing
// either to authorize the app. A subsequent completed tap on ALLOW can grant.
enum class NativeConsentDecision { Pending, Deny, Allow };

class NativeConsentInputGate final {
 public:
  NativeConsentDecision sample(bool cancelDown, bool touchIdle,
                               bool touchDenyTap, bool touchAllowTap) {
    // Cancellation, including an on-screen DENY tap, always wins.
    if (cancelDown || touchDenyTap) return NativeConsentDecision::Deny;
    if (!touchArmed_) {
      if (touchIdle) touchArmed_ = true;
      return NativeConsentDecision::Pending;
    }
    return touchAllowTap ? NativeConsentDecision::Allow
                         : NativeConsentDecision::Pending;
  }

 private:
  bool touchArmed_ = false;
};
