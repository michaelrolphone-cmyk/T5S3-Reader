#pragma once

class GfxRenderer;
class HalGPIO;

namespace DeskClockSleep {
// Called on the main task after SleepActivity has closed the previous screen.
// Returns only on user wake or a sleep-configuration error.
void run(GfxRenderer& renderer, HalGPIO& input);
}  // namespace DeskClockSleep
