#pragma once

// Firmware-only unload guard for the GameBoy-derived raw EPD video service.
// Safe to call when the service was never started.
bool nativeVideoForceStop();

#include <T5VideoApi.h>
// Internal cold-boot entry, not an ELF import. Starts the existing mono service
// with a bounded spatial endpoint scrub in place of its global reset flashes.
bool nativeVideoStartBootScrub(t5_video_surface_v1* surface);
