#pragma once

#include <cstdint>

namespace RuntimeNetwork {

// Firmware-owned reconnect policy used by native network APIs.
// Returns immediately when a usable station connection already exists.
// Otherwise attempts the last-connected saved SSID first, then remaining
// distinct saved profiles, sharing one timeout budget for a usable IP connection.
bool ensureSavedConnection(uint32_t timeoutMs = 15000u);

}  // namespace RuntimeNetwork
