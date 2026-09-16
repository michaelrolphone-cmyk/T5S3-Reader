#include "native/NativeConsentInputGate.h"
#include <cassert>
#include <cstdio>

int main() {
  using Decision = NativeConsentDecision;
  // A Confirm held before showing the trusted screen cannot be counted.
  NativeConsentInputGate held;
  assert(held.sample(true, true, false, false) == Decision::Pending);
  assert(held.sample(true, false, false, false) == Decision::Pending);
  assert(held.sample(false, false, false, false) == Decision::Pending);
  assert(held.sample(true, false, false, false) == Decision::Pending);
  assert(held.sample(true, true, false, false) == Decision::Allow);

  // A stale button edge on the first frame cannot approve, even if up.
  NativeConsentInputGate stale;
  assert(stale.sample(false, true, false, false) == Decision::Pending);
  assert(stale.sample(false, false, false, false) == Decision::Pending);
  assert(stale.sample(true, true, false, false) == Decision::Allow);

  // Rejection wins over simultaneous new presses; a prior tap has no approve
  // input at all. Only a touch specifically on DENY is ever considered.
  NativeConsentInputGate denied;
  assert(denied.sample(false, false, false, false) == Decision::Pending);
  assert(denied.sample(true, true, true, false) == Decision::Deny);
  NativeConsentInputGate touch;
  assert(touch.sample(false, false, false, true) == Decision::Deny);
  NativeConsentInputGate noTouchApproval;
  assert(noTouchApproval.sample(false, false, false, false) == Decision::Pending);
  for (unsigned i = 0; i < 10; ++i)
    assert(noTouchApproval.sample(false, false, false, false) == Decision::Pending);
  assert(noTouchApproval.sample(true, true, false, false) == Decision::Allow);
  std::puts("Consent requires a fresh hardware edge; stale touch and held input cannot approve");
}
