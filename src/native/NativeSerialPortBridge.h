#pragma once

#include "runtime/capabilities/SerialProviderRegistry.h"
#include <cstdint>

void nativeSerialPortsBegin();
void nativeSerialPortsEnd();

// Trusted firmware only: never exported through the native app ELF ABI.
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
