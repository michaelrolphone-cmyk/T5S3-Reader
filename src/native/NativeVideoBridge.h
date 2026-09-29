#pragma once

// Firmware-only unload guard for the GameBoy-derived raw EPD video service.
// Safe to call when the service was never started.
bool nativeVideoForceStop();
