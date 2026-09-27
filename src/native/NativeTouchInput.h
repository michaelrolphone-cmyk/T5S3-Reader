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
bool nativeTouchGetTap(NativeTouchPoint& point);
bool nativeTouchGetHold(NativeTouchPoint& point, unsigned long& heldMs);
bool nativeTouchGetSwipe(NativeTouchPoint& start, NativeTouchPoint& end);
bool nativeTouchTakeHomePress();
