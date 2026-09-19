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

// serial.port is the only firmware USB serial consumer. Direct open_usb
// streams no longer claim the physical device or a capability lease.
// usb.serial acquire/configure/control/status go through NativeUsbClassBridge
// (installed class ELF from the provider graph), not t5_usb_get_api().
// Acquire returns ClassStreamSession RX/TX after attachPublished.
