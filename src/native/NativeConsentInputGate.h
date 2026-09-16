#pragma once

// Pure, firmware-internal consent input policy. A button held when the prompt
// opens cannot approve until released and freshly pressed. A preexisting tap
// cannot approve: touch is intentionally permitted to DENY only until a
// reliable hardware-level touch-down/release epoch is available.
enum class NativeConsentDecision { Pending, Deny, Allow };

class NativeConsentInputGate final {
 public:
  NativeConsentDecision sample(bool confirmDown, bool confirmPressed,
                               bool cancelDown, bool touchDenyTap) {
    if (cancelDown || touchDenyTap) return NativeConsentDecision::Deny;
    if (!confirmReleased_) {
      if (!confirmDown) confirmReleased_ = true;
      return NativeConsentDecision::Pending;
    }
    return confirmDown && confirmPressed ? NativeConsentDecision::Allow
                                         : NativeConsentDecision::Pending;
  }

 private:
  bool confirmReleased_ = false;
};
