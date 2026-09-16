#pragma once

#include "runtime/capabilities/SerialProviderRegistry.h"
#include "runtime/capabilities/DeviceRegistry.h"
#include <cstdint>
#include <cstring>

void nativeSerialPortsBegin();
void nativeSerialPortsEnd();

// Trusted firmware only: never exported through the native app ELF ABI.
// Called by the main owner-task loop and native app's owner-task input poll.
// Copies the host's lock-protected enumeration snapshot into the unified
// device registry; it never starts/stops USB or claims a capability lease.
// Never call from a USB callback, render task or stream scheduler.
void nativeDeviceDiscoveryTick();

// Register/unregister on the native app owner task while the selected provider
// is idle. The callback code and context must outlive their registration.
// USB is registered permanently as the default provider and resident fallback.
bool nativeRegisterSerialProvider(const RuntimeSerial::Provider& provider);
bool nativeUnregisterSerialProvider(const char* id);

// Direct compatibility USB streams share the serial bridge's physical
// serial.port capability lease and invocation identity. Call only on the
// application owner task, outside the stream registry mutex. The epoch is
// captured at stream open and prevents rebinding to a replacement device.
bool nativeUsbDirectStreamClaim(uint32_t expectedEpoch);
void nativeUsbDirectStreamRelease();

// The claim hook succeeds during initial enumeration to reserve the session.
// Data must NOT flow until its physical interface is actually in the common
// registry and the claim hook has acquired its exclusive lease.
inline bool nativeUsbDirectStreamBound() {
  auto& registry = RuntimeDevices::systemRegistry();
  RuntimeDevices::DeviceInfo info{};
  for (size_t index = 0; index < RuntimeDevices::kMaxDevices; ++index) {
    if (registry.at(index, &info) && info.transport == RuntimeDevices::Transport::Usb &&
        std::strcmp(info.provider, "usb.serial") == 0 &&
        info.state == RuntimeDevices::State::Available) return true;
  }
  return false;
}
