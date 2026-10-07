#pragma once

#include <cstdint>

// Copied, validity-aware view of board.battery v1. No provider pointer escapes
// the owner task. This API does not supply VBUS, full, current or capacity data.
struct NativeBatterySnapshot {
  uint16_t millivolts = 0;
  uint8_t percent = 0;
  bool charging = false;
};

// Called only by MappedInputManager::update on the serialized invocation-owner
// task (including native-app input updates). The first call binds that task;
// calls from any other task do nothing. Optional acquisition retries are slow,
// and no provider is required for Home to continue. No effect on legacy boards.
void nativeBatteryTick();

// Safe on render/other tasks: copies only, never discovers/loads/polls an ELF.
// Returns false and clears out before the first good sample, after a read
// failure, or once the last sample is 15 seconds old. Zero percent is valid
// only when this returns true. The generation-qualified lease stays owned by
// the tick until checked owner-task sleep suspension.
bool nativeBatteryReadSnapshot(NativeBatterySnapshot* out);

// Owner-task lifecycle. Failed release retains the exact revoked grant; resume
// must finish its cleanup before a fresh sample generation can be acquired.
bool nativeBatterySuspend();
bool nativeBatteryResume();
