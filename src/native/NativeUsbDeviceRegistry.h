#pragma once

#include <T5SerialPortApi.h>
#include <T5UsbApi.h>
#include <cstdint>
#include <cstring>
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <Logging.h>
#else
#include <atomic>
#endif

// Runtime-only device identities: USB handles, descriptors and events never
// cross the serial.port application ABI.
namespace NativeUsbDevices {

enum class Transport : uint8_t { Usb = 1 };
enum class Provider : uint8_t { None = 0, UsbSerial = 1 };
enum class Presence : uint8_t { Unavailable = 0, Bound = 1 };
enum class RevocationCause : uint8_t {
  None = 0, DetachNotification = 1, ConnectionLost = 2,
  HostStopped = 3, BindingChanged = 4,
};

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

// Coherent bounded snapshot. A detach notification also occurs on intentional
// lease/app teardown: it is NOT by itself evidence of a physical unplug.
// These are claimed-interface counters, not pre-claim enumeration statistics.
struct Diagnostics {
  Record device{};
  uint32_t epoch = 0;
  uint32_t binds = 0;
  uint32_t revocations = 0;
  uint32_t detach_notifications = 0;
  uint32_t connection_losses = 0;
  uint32_t host_stops = 0;
  uint32_t binding_changes = 0;
  RevocationCause last_cause = RevocationCause::None;
};

// USB host publishes attachment/detachment on a separate core. Keep the
// snapshot and counters under one portMUX, but NEVER log while holding it.
// Generations, epochs and counts persist across app unload; counts saturate.
class Registry final {
 public:
  void observe(const t5_usb_serial_state_t& state, uint8_t interface_number = 0xff) {
    Diagnostics event{};
    bool changed = false;
    {
      Guard guard(lock_);
      if (!state.connected || state.status == T5_USB_STATUS_OFF) {
        changed = record_.presence == Presence::Bound;
        invalidateLocked(state.status == T5_USB_STATUS_OFF ?
            RevocationCause::HostStopped : RevocationCause::ConnectionLost);
      } else {
        const bool different = record_.presence != Presence::Bound ||
            record_.vid != state.vid || record_.pid != state.pid ||
            record_.interface_number != interface_number ||
            std::strncmp(record_.product, state.product, sizeof(record_.product)) != 0;
        if (different) {
          changed = true;
          invalidateLocked(RevocationCause::BindingChanged);
          // Never wrap an opaque identity or a lease-revocation token.
          if (generation_ < 0x7fffffffu && epoch_ != UINT32_MAX) {
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
            increment(diag_.binds);
          }
        }
      }
      if (changed) event = diagnosticsLocked();
    }
    if (changed) logTransition(event);
  }

  void detach() {
    Diagnostics event{};
    bool changed = false;
    {
      Guard guard(lock_);
      changed = record_.presence == Presence::Bound;
      invalidateLocked(RevocationCause::DetachNotification);
      if (changed) event = diagnosticsLocked();
    }
    if (changed) logTransition(event);
  }

  Record snapshot() const {
    Guard guard(lock_);
    return record_;
  }
  Diagnostics diagnostics() const {
    Guard guard(lock_);
    return diagnosticsLocked();
  }
  uint32_t epoch() const {
    Guard guard(lock_);
    return epoch_;
  }
  bool resolve(t5_serial_device_t id, Provider provider) const {
    Guard guard(lock_);
    return id != 0 && record_.presence == Presence::Bound &&
           record_.id == id && record_.provider == provider && record_.serial_port;
  }

 private:
  static void increment(uint32_t& value) {
    if (value != UINT32_MAX) ++value;
  }
  Diagnostics diagnosticsLocked() const {
    Diagnostics result = diag_;
    result.device = record_;
    result.epoch = epoch_;
    return result;
  }
  static void logTransition(const Diagnostics& event) {
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
    // Capture event under the lock, then log after it. The binding epoch and
    // opaque ID correlate with serial leases; no USB pointers are exposed.
    LOG_INF("USB", "USBREF binding=%s epoch=%lu id=%lu cause=%u vid=%04X pid=%04X iface=%u binds=%lu revocations=%lu",
            event.device.id ? "bound" : "revoked",
            static_cast<unsigned long>(event.epoch),
            static_cast<unsigned long>(event.device.id),
            static_cast<unsigned>(event.last_cause),
            static_cast<unsigned>(event.device.vid),
            static_cast<unsigned>(event.device.pid),
            static_cast<unsigned>(event.device.interface_number),
            static_cast<unsigned long>(event.binds),
            static_cast<unsigned long>(event.revocations));
#else
    (void)event;
#endif
  }
  void invalidateLocked(RevocationCause cause) {
    if (record_.presence == Presence::Bound) {
      if (epoch_ != UINT32_MAX) ++epoch_;
      increment(diag_.revocations);
      diag_.last_cause = cause;
      switch (cause) {
        case RevocationCause::DetachNotification: increment(diag_.detach_notifications); break;
        case RevocationCause::ConnectionLost: increment(diag_.connection_losses); break;
        case RevocationCause::HostStopped: increment(diag_.host_stops); break;
        case RevocationCause::BindingChanged: increment(diag_.binding_changes); break;
        default: break;
      }
    }
    record_ = Record{};
  }
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
  Diagnostics diag_{};
  uint32_t generation_ = 0;
  uint32_t epoch_ = 0;
};

}  // namespace NativeUsbDevices

// Firmware-only host lifecycle hooks; not ELF imports.
void nativeUsbProviderAttach(const t5_usb_serial_state_t* state, uint8_t dataInterface);
void nativeUsbProviderDetach();
uint32_t nativeUsbProviderEpoch();
