#pragma once
#include <T5DriverApi.h>
#include <T5UsbClassDriver.h>

// Runtime-only module: never hand an ELF function table to applications or
// retain pointers to USB host descriptors across a callback.
class UsbCdcDriverModule final {
 public:
  enum class State { Absent, Loaded, Active, Failed };
  bool load(const char* validatedElfPath);
  bool unload();
  bool probe(const uint8_t* config, size_t length, uint16_t vid, uint16_t pid,
             t5_usb_cdc_binding_v1* out) const;
  bool lineCoding(uint32_t baud, uint8_t bits, uint8_t parity, uint8_t stop,
                  uint8_t payload[7]) const;
  bool controlLines(bool dtr, bool rts, uint16_t* out) const;
  State state() const { return state_; }
 private:
  void* handle_ = nullptr;
  const t5_driver_v1* driver_ = nullptr;
  const t5_usb_cdc_class_api_v1* api_ = nullptr;
  State state_ = State::Absent;
};
