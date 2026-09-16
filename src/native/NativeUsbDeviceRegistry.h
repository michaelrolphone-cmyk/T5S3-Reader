#pragma once

#include <T5SerialPortApi.h>
#include <T5UsbApi.h>
#include <cstdint>
#include <cstring>
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#else
#include <atomic>
#endif

// Internal runtime device records. Applications see only opaque IDs through
// serial.port; USB host handles, endpoints, descriptors and event APIs are not
// exported through the application ABI.
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
  uint8_t interface_number = 0xff;
  bool serial_port = false;
  char product[T5_USB_PRODUCT_MAX] = {};
};

// USB host task publishes attachment/detachment immediately. The serial app
// task concurrently takes snapshots and resolves selected IDs; all operations
// on this small record are synchronized. ESP32 disables task preemption while
// holding its cross-core portMUX: an atomic spin lock alone could deadlock if
// a higher-priority USB host task preempted its owner on the same core.
// Generation never resets on detach, USB restart or application unload.
class Registry final {
 public:
  void observe(const t5_usb_serial_state_t& state, uint8_t interface_number = 0xff) {
    Guard guard(lock_);
    if (!state.connected || state.status == T5_USB_STATUS_OFF) {
      record_ = Record{};
      return;
    }
    const bool different = record_.presence != Presence::Bound ||
        record_.vid != state.vid || record_.pid != state.pid ||
        record_.interface_number != interface_number ||
        std::strncmp(record_.product, state.product, sizeof(record_.product)) != 0;
    if (!different) return;
    record_ = Record{};
    // Do not recycle an identity if the finite handle namespace is exhausted.
    if (generation_ >= 0x7fffffffu) return;
    ++generation_;
    record_.id = (generation_ << 1u) | 1u;
    record_.transport = Transport::Usb;
    record_.provider = Provider::UsbSerial;
    record_.presence = Presence::Bound;
    record_.vid = state.vid;
    record_.pid = state.pid;
    record_.interface_number = interface_number;
    record_.serial_port = true;
    std::memcpy(record_.product, state.product, sizeof(record_.product));
    record_.product[sizeof(record_.product) - 1u] = '\0';
  }

  void detach() {
    Guard guard(lock_);
    record_ = Record{};
  }

  Record snapshot() const {
    Guard guard(lock_);
    return record_;
  }

  bool resolve(t5_serial_device_t id, Provider provider) const {
    Guard guard(lock_);
    return id != 0 && record_.presence == Presence::Bound &&
           record_.id == id && record_.provider == provider && record_.serial_port;
  }

 private:
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
  using Mutex = portMUX_TYPE;
  struct Guard {
    explicit Guard(Mutex& mux) : mux_(mux) { portENTER_CRITICAL(&mux_); }
    ~Guard() { portEXIT_CRITICAL(&mux_); }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
    Mutex& mux_;
  };
  mutable Mutex lock_ = portMUX_INITIALIZER_UNLOCKED;
#else
  using Mutex = std::atomic_flag;
  struct Guard {
    explicit Guard(Mutex& flag) : flag_(flag) {
      while (flag_.test_and_set(std::memory_order_acquire)) {}
    }
    ~Guard() { flag_.clear(std::memory_order_release); }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
    Mutex& flag_;
  };
  mutable Mutex lock_ = ATOMIC_FLAG_INIT;
#endif
  Record record_{};
  uint32_t generation_ = 0;
};

}  // namespace NativeUsbDevices

// Runtime-only publication hooks. The USB host task invokes these at the
// lifecycle boundary; they are not exported to ELF applications.
void nativeUsbProviderAttach(const t5_usb_serial_state_t* state, uint8_t dataInterface);
void nativeUsbProviderDetach();
