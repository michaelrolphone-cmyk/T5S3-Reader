#pragma once

#include "runtime/capabilities/DeviceRegistry.h"
#include <cstdint>

// Trusted firmware only. Called synchronously on the active native app owner
// task, with the app ELF paused; no ELF symbol exports this entry point.
// A false answer includes cancellation, timeout and unavailable UI. The
// caller must revalidate the exact device generation and execution context
// after the prompt before issuing a transient grant.
bool nativeDeviceConsentPrompt(const RuntimeDevices::DeviceInfo& device,
                               const char* capability, uint32_t rights);
