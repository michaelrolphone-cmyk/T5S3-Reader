#pragma once
#include <T5SerialPortApi.h>
#include <T5UsbApi.h>
#include "runtime/usb/UsbClassStreamSession.h"
#include <cstdint>

// Firmware-internal class-ELF control and data plane. Serial Monitor and the
// programmer call these instead of t5_usb_get_api(). Bytes move on published
// serial.port endpoints owned by ClassStreamSession.
bool nativeUsbClassAvailable();
bool nativeUsbClassEnsureInstalled();
bool nativeUsbClassHasDataPlane();
bool nativeUsbClassStart(const t5_serial_config_t& config);
void nativeUsbClassStop();
bool nativeUsbClassConfigure(const t5_serial_config_t& config);
bool nativeUsbClassControl(bool dtr, bool rts);
bool nativeUsbClassReadState(t5_usb_serial_state_t* out);
int32_t nativeUsbClassRead(uint8_t* dst, uint32_t capacity, uint32_t* out);
int32_t nativeUsbClassWrite(const uint8_t* src, uint32_t length, uint32_t* out);
uint64_t nativeUsbClassToken();
void nativeUsbClassObserveDevice(uint64_t device);
bool nativeUsbClassAdopt(uint64_t opened, const t5_serial_config_t& config);

// Bind an installed class ELF. The function table must outlive the bind.
// A missing bind makes usb.serial unavailable (no firmware USB fallback).
struct NativeUsbClassOps {
  void* context = nullptr;
  uint64_t (*open)(void*, uint64_t device) = nullptr;
  bool (*configure)(void*, uint64_t token, uint32_t baud, uint8_t bits,
                    uint8_t parity, uint8_t stop_bits) = nullptr;
  bool (*control)(void*, uint64_t token, bool dtr, bool rts) = nullptr;
  int32_t (*read)(void*, uint64_t token, uint8_t* dst, uint32_t capacity,
                  uint32_t timeout_ms) = nullptr;
  int32_t (*write)(void*, uint64_t token, const uint8_t* src, uint32_t length,
                   uint32_t timeout_ms) = nullptr;
  bool (*close)(void*, uint64_t token) = nullptr;
};
bool nativeUsbClassBind(const NativeUsbClassOps& ops);
bool nativeUsbClassBindPort(const RuntimeUsb::ClassPort& port);
// `api` is a risc_usb_cdc_api_v1 published by usb-cdc-acm-v2 / usb-cp210x-v2.
bool nativeUsbClassBindApi(const void* riscUsbCdcApiV1);
void nativeUsbClassUnbind();

RuntimeUsb::ClassStreamSession& nativeUsbClassSession();
