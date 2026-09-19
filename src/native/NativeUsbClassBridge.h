#pragma once
#include <T5SerialPortApi.h>
#include <T5UsbApi.h>
#include <cstdint>

// Firmware-internal class-ELF control plane. Serial Monitor and the
// programmer call these instead of t5_usb_get_api(). Data bytes still move
// through published serial.port endpoints.
bool nativeUsbClassAvailable();
bool nativeUsbClassStart(const t5_serial_config_t& config);
void nativeUsbClassStop();
bool nativeUsbClassConfigure(const t5_serial_config_t& config);
bool nativeUsbClassControl(bool dtr, bool rts);
bool nativeUsbClassReadState(t5_usb_serial_state_t* out);

// Bind an installed class ELF. The function table must outlive the bind.
// A missing bind makes usb.serial unavailable (no firmware USB fallback).
struct NativeUsbClassOps {
  void* context = nullptr;
  uint64_t (*open)(void*, uint64_t device) = nullptr;
  bool (*configure)(void*, uint64_t token, uint32_t baud, uint8_t bits,
                    uint8_t parity, uint8_t stop_bits) = nullptr;
  bool (*control)(void*, uint64_t token, bool dtr, bool rts) = nullptr;
  bool (*close)(void*, uint64_t token) = nullptr;
};
bool nativeUsbClassBind(const NativeUsbClassOps& ops);
void nativeUsbClassUnbind();
