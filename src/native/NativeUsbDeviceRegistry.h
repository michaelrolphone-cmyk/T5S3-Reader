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

// Runtime-only identities: USB handles, descriptors and events never cross
// the serial.port application ABI.
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

// Coherent bounded snapshot. Last revoked identity remains available after
// the active record has been cleared, so a detach can be correlated with its
// former serial lease. A detach notification can be intentional teardown.
// These are claimed-interface counters, NOT enumeration statistics.
struct Diagnostics {
  Record device{};
  Record last_revoked{};
  uint32_t epoch = 0;
  uint32_t binds = 0;
  uint32_t revocations = 0;
  uint32_t detach_notifications = 0;
  uint32_t connection_losses = 0;
  uint32_t host_stops = 0;
  uint32_t binding_changes = 0;
  RevocationCause last_cause = RevocationCause::None;
};

// Host and consumers run on separate cores. Copy event snapshots inside the
// lock; do NOT invoke a formatter, logger or callbacks while holding it.
// Generations, epochs and counters persist across app unload and never wrap.
class Registry final {
 public:
  void observe(const t5_usb_serial_state_t& state, uint8_t interface_number = 0xff) {
    Diagnostics revokeEvent{}, bindEvent{};
    bool revoked = false, bound = false;
    {
      Guard guard(lock_);
      if (!state.connected || state.status == T5_USB_STATUS_OFF) {
        revoked = record_.presence == Presence::Bound;
        invalidateLocked(state.status == T5_USB_STATUS_OFF ?
            RevocationCause::HostStopped : RevocationCause::ConnectionLost);
        if (revoked) revokeEvent = diagnosticsLocked();
      } else {
        const bool different = record_.presence != Presence::Bound ||
            record_.vid != state.vid || record_.pid != state.pid ||
            record_.interface_number != interface_number ||
            std::strncmp(record_.product, state.product, sizeof(record_.product)) != 0;
        if (different) {
          revoked = record_.presence == Presence::Bound;
          invalidateLocked(RevocationCause::BindingChanged);
          if (revoked) revokeEvent = diagnosticsLocked();
          // Do not allow stale IDs or epochs to alias newly claimed devices.
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
            bound = true;
            bindEvent = diagnosticsLocked();
          }
        }
      }
    }
    // Replacement emits TWO distinct events in order: old device revoked,
    // then new device bound. An initial/replug bind has cause=0, not the
    // lingering last revocation cause in the diagnostic snapshot.
    if (revoked) logTransition(revokeEvent, false);
    if (bound) logTransition(bindEvent, true);
  }

  void detach() {
    Diagnostics event{};
    bool revoked = false;
    {
      Guard guard(lock_);
      revoked = record_.presence == Presence::Bound;
      invalidateLocked(RevocationCause::DetachNotification);
      if (revoked) event = diagnosticsLocked();
    }
    if (revoked) logTransition(event, false);
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
  static void logTransition(const Diagnostics& event, bool binding) {
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
    const Record& identity = binding ? event.device : event.last_revoked;
    LOG_INF("USB", "USBREF event=%s epoch=%lu id=%lu cause=%u vid=%04X pid=%04X iface=%u binds=%lu revocations=%lu",
            binding ? "bind" : "revoke",
            static_cast<unsigned long>(event.epoch),
            static_cast<unsigned long>(identity.id),
            static_cast<unsigned>(binding ? RevocationCause::None : event.last_cause),
            static_cast<unsigned>(identity.vid),
            static_cast<unsigned>(identity.pid),
            static_cast<unsigned>(identity.interface_number),
            static_cast<unsigned long>(event.binds),
            static_cast<unsigned long>(event.revocations));
#else
    (void)event;
    (void)binding;
#endif
  }
  void invalidateLocked(RevocationCause cause) {
    if (record_.presence == Presence::Bound) {
      diag_.last_revoked = record_;
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
