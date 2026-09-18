#include "native/NativeConsentInputGate.h"
#include <cassert>
#include <cstdio>

int main() {
  using Decision = NativeConsentDecision;

  // A tap inherited from the launching app never grants before the trusted
  // screen observes an idle touch frame, even when it lands on ALLOW.
  NativeConsentInputGate stale;
  assert(stale.sample(false, false, false, true) == Decision::Pending);
  assert(stale.sample(false, false, false, false) == Decision::Pending);
  assert(stale.sample(false, true, false, true) == Decision::Pending);
  assert(stale.sample(false, false, false, true) == Decision::Allow);

  // Once a clean frame has armed the dialog, the two on-screen buttons must
  // perform precisely the actions printed on them.
  NativeConsentInputGate allow;
  assert(allow.sample(false, true, false, false) == Decision::Pending);
  assert(allow.sample(false, false, false, true) == Decision::Allow);
  NativeConsentInputGate deny;
  assert(deny.sample(false, true, false, false) == Decision::Pending);
  assert(deny.sample(false, false, true, false) == Decision::Deny);

  // A DENY tap is always safe, including before the touch idle epoch; the
  // cancel control wins over simultaneous approval after arming.
  NativeConsentInputGate earlyDeny;
  assert(earlyDeny.sample(false, false, true, true) == Decision::Deny);
  NativeConsentInputGate cancel;
  assert(cancel.sample(false, true, false, false) == Decision::Pending);
  assert(cancel.sample(true, false, false, true) == Decision::Deny);

  NativeConsentInputGate outside;
  assert(outside.sample(false, true, false, false) == Decision::Pending);
  for (unsigned i = 0; i < 10; ++i)
    assert(outside.sample(false, true, false, false) == Decision::Pending);
  std::puts("Consent touch gate: fresh Allow, Deny, stale input and cancel PASS");
}
