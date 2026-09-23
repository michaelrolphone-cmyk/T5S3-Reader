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
}  // namespace DeskClockSleep
