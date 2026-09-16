#pragma once

#include "runtime/capabilities/SerialProviderRegistry.h"

void nativeSerialPortsBegin();
void nativeSerialPortsEnd();

// Trusted firmware only: never exported through the native app ELF ABI.
// Register/unregister on the native app owner task while the selected provider
// is idle. The callback code and context must outlive their registration.
// USB is registered permanently as the default provider and resident fallback.
bool nativeRegisterSerialProvider(const RuntimeSerial::Provider& provider);
bool nativeUnregisterSerialProvider(const char* id);
