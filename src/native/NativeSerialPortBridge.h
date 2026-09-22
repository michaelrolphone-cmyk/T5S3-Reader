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
// Production's built-in provider resolves installed semantic serial.port
// devices; it does not contain a transport-specific fallback.
bool nativeRegisterSerialProvider(const RuntimeSerial::Provider& provider);
bool nativeUnregisterSerialProvider(const char* id);

// Transport-neutral stream shuttle hooks. The stream scheduler may call these
// outside its registry mutex; provider selection/discovery remains owner-task
// only. Epoch 0 means no usable session. No provider interface pointer or
// physical device token is exposed to applications.
bool nativeSerialProviderActive();
uint32_t nativeSerialProviderEpoch();
int32_t nativeSerialProviderRead(uint8_t* dst, uint32_t capacity, uint32_t* out);
int32_t nativeSerialProviderWrite(const uint8_t* src, uint32_t length, uint32_t* out);
