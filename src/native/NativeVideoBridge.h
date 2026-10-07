#pragma once

// Firmware-only unload guard for the GameBoy-derived raw EPD video service.
// Safe to call when the service was never started.
bool nativeVideoForceStop();

// Bounded owner-task observation of an accepted UI-video frame. Opens a
// pending touch surface only after the exact owning invocation's scan settles.
void nativeVideoServiceTouchPresentation();
