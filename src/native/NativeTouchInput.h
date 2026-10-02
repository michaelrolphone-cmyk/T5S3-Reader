#pragma once
#include <stdint.h>

struct NativeTouchPoint {
  uint16_t x = 0;
  uint16_t y = 0;
};

// Firmware-side consumer of the installed input.touch.raw provider.
// Provider sampling runs on the dedicated capture task; UI-facing getters and
// lifecycle entry points are concurrency-safe and do not depend on render/UI cadence.
void nativeTouchTick();
bool nativeTouchResume();
bool nativeTouchSuspend();
bool nativeTouchAvailable();
bool nativeTouchHadActivity();
// Start a new focus generation without dropping the provider. Clears delivered
// gestures now; the capture task fences raw backlog with its next successful
// poll/snapshot. Held contacts must lift before becoming eligible.
void nativeTouchDiscardGestures();
bool nativeTouchGetTap(NativeTouchPoint& point);
bool nativeTouchGetContact(NativeTouchPoint& point);
bool nativeTouchGetHold(NativeTouchPoint& point, unsigned long& heldMs);
bool nativeTouchGetSwipe(NativeTouchPoint& start, NativeTouchPoint& end);
// One-shot capture timestamp, retained even when the UI polls late.
bool nativeTouchTakeHomePress(unsigned long& eventMs);
bool nativeTouchTakeHomePress();

// Copied counters for diagnostics; no provider pointers escape the consumer.
struct NativeTouchDiagnostics {
  uint32_t polls = 0, pollFailures = 0, gaps = 0, events = 0;
  uint32_t taps = 0, tapOverflows = 0, outages = 0;
  uint32_t maxServiceMs = 0, maxCaptureGapMs = 0;
};
NativeTouchDiagnostics nativeTouchDiagnostics();
