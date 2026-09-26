#pragma once
#include <stdint.h>

struct NativeTouchPoint {
  uint16_t x = 0;
  uint16_t y = 0;
};

// Firmware-side consumer of the installed input.touch.raw provider.
// All calls run on the serialized firmware/app owner task.
void nativeTouchTick();
bool nativeTouchResume();
bool nativeTouchSuspend();
bool nativeTouchAvailable();
bool nativeTouchHadActivity();
bool nativeTouchGetTap(NativeTouchPoint& point);
bool nativeTouchGetHold(NativeTouchPoint& point, unsigned long& heldMs);
bool nativeTouchGetSwipe(NativeTouchPoint& start, NativeTouchPoint& end);
bool nativeTouchTakeHomePress();
