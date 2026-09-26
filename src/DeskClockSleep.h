#pragma once

class GfxRenderer;
class HalGPIO;

namespace DeskClockSleep {
// Enter the clock after the current activity has saved its state. This paints
// the first frame and enters deep sleep with a minute timer and button wake.
// Returns only if a wake source cannot be configured.
void run(GfxRenderer& renderer, HalGPIO& input);

// Called before normal firmware/SD/UI initialization. On a retained clock timer
// wake, initializes only the board, clock and display, paints and sleeps again.
// Returns false for ordinary boots and if timer-resume fails, allowing normal
// firmware startup. A successful timer-resume never returns.
bool resumeAfterTimerWake();

// Returns true once when normal startup is continuing because the user woke a
// retained desk clock with its button wake source. Timer wakes are handled
// entirely inside resumeAfterTimerWake() and never reach normal startup.
bool consumeUserWake();
}  // namespace DeskClockSleep
