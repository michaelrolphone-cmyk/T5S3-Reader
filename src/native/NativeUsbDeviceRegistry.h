#pragma once

#include <T5SerialPortApi.h>
#include <T5UsbApi.h>
#include <cstdint>
#include <cstring>

// Internal runtime device record: never expose the USB implementation, host
// handle, endpoint, or transfer descriptors through the application ABI.
// This first slice has one USB serial binding. The record/resolver can be
// extended to more USB interfaces and providers without changing serial.port.
namespace NativeUsbDevices {

enum class Transport : uint8_t { Usb = 1 };
enum class Provider : uint8_t { None = 0, UsbSerial = 1 };
enum class Presence : uint8_t { Unavailable = 0, Bound = 1 };

struct Record {
  t5_serial_device_t id = 0;
  Transport transport = Transport::Usb;
  Provider provider = Provider::None;
  Presence presence = Presence::Unavailable;
  uint16_t vid = 0;
  uint16_t pid = 0;
  // USB serial status does not yet export the claimed interface number. An
  // unknown interface is explicit, not an invented interface zero.
  uint8_t interface_number = 0xff;
  bool serial_port = false;
  char product[T5_USB_PRODUCT_MAX] = {};
};

// Only the runtime/provider calls observe()/detach(); an application receives
// the opaque ID through serial.port status and may request that exact ID.
// The caller serializes access to this object when observations originate from
// multiple tasks. This implementation's owner is the serial provider bridge.
class Registry final {
 public:
  void observe(const t5_usb_serial_state_t& state) {
    if (!state.connected || state.status == T5_USB_STATUS_OFF) {
      detach();
      return;
    }
    const bool different = record_.presence != Presence::Bound ||
        record_.vid != state.vid || record_.pid != state.pid ||
        std::strncmp(record_.product, state.product, sizeof(record_.product)) != 0;
    if (!different) return;
    detach();
    // Fail closed on exhaustion rather than allowing a stale identity to
    // identify a different device after generation wraparound.
    if (generation_ >= 0x7fffffffu) return;
    ++generation_;
    record_.id = (generation_ << 1u) | 1u;
    record_.transport = Transport::Usb;
    record_.provider = Provider::UsbSerial;
    record_.presence = Presence::Bound;
    record_.vid = state.vid;
    record_.pid = state.pid;
    record_.serial_port = true;
    std::memcpy(record_.product, state.product, sizeof(record_.product));
    record_.product[sizeof(record_.product) - 1u] = '\0';
  }

  void detach() { record_ = Record{}; }

  Record snapshot() const { return record_; }

  bool resolve(t5_serial_device_t id, Provider provider) const {
    return id != 0 && record_.presence == Presence::Bound &&
           record_.id == id && record_.provider == provider && record_.serial_port;
  }

 private:
  Record record_{};
  uint32_t generation_ = 0;  // Persistent across app contexts/USB sessions.
};

}  // namespace NativeUsbDevices
