#pragma once

#include <T5UsbClassDriver.h>
#include <cstddef>
#include <cstdint>

// Internal, host-task-only dispatch. No driver ELF pointers escape the runtime.
// Calls must be serialized by the owning USB host task, including deactivate().
namespace UsbCdcDriverRuntime {

// Verify the installed package's manifest/ELF hash and ABI before dlopen.
// An absent, invalid, or unload-failed driver is unavailable; the existing
// resident CDC implementation remains the compatibility fallback.
bool activate();
bool active();

// Call only after the host has stopped dispatching transfers and deregistered
// its USB client. A failed dlclose retains its handle and returns false.
bool deactivate();

bool probe(const uint8_t* configuration, size_t length, uint16_t vid, uint16_t pid,
           t5_usb_cdc_binding_v1* binding);
bool lineCoding(uint32_t baud, uint8_t bits, uint8_t parity, uint8_t stop,
                uint8_t payload[7]);
bool controlLines(bool dtr, bool rts, uint16_t* value);

}  // namespace UsbCdcDriverRuntime
