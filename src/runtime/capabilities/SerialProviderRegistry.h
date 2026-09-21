#pragma once

#include <T5SerialPortApi.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Semantic serial providers own their own lifecycle; the owner task serializes
// registrations and lease calls. Failed physical teardown must not forget the
// public lease while the provider still owns callbacks/DMA or a package pin.
namespace RuntimeSerial {
struct Provider {
  const char* id = nullptr;
  uint8_t priority = 100;
  void* context = nullptr;
  bool (*available)(void*) = nullptr;
  bool (*matches)(void*, t5_serial_device_t) = nullptr;
  t5_serial_result_t (*acquire)(void*, const t5_serial_port_request_t*,
                                t5_serial_port_lease_t*, t5_stream_t*, t5_stream_t*) = nullptr;
  t5_serial_result_t (*configure)(void*, t5_serial_port_lease_t,
                                  const t5_serial_config_t*) = nullptr;
  t5_serial_result_t (*read_status)(void*, t5_serial_port_lease_t,
                                    t5_serial_port_state_t*) = nullptr;
  t5_serial_result_t (*set_control_lines)(void*, t5_serial_port_lease_t,
                                          bool, bool) = nullptr;
  t5_serial_result_t (*release)(void*, t5_serial_port_lease_t) = nullptr;
};

class Registry final {
 public:
  static constexpr size_t kMaxProviders = 4;
  static constexpr size_t kNameBytes = 32;

  bool add(const Provider& provider) {
    if (active_ || releasing_ || !provider.id || !provider.available || !provider.matches ||
        !provider.acquire || !provider.configure || !provider.read_status ||
        !provider.set_control_lines || !provider.release) return false;
    const size_t length = strnlen(provider.id, kNameBytes);
    if (!length || length == kNameBytes) return false;
    Slot* empty = nullptr;
    for (auto& slot : slots_) {
      if (slot.used && std::strcmp(slot.name, provider.id) == 0) return false;
      if (!slot.used && !empty) empty = &slot;
    }
    if (!empty) return false;
    empty->provider = provider;
    std::memcpy(empty->name, provider.id, length + 1);
    empty->used = true;
    return true;
  }

  bool remove(const char* id) {
    if (!id || releasing_) return false;
    for (auto& slot : slots_) {
      if (!slot.used || std::strcmp(slot.name, id) != 0) continue;
      if (&slot == active_) return false;
      slot = Slot{};
      return true;
    }
    return false;
  }

  t5_serial_result_t acquire(const t5_serial_port_request_t* request,
                             t5_serial_port_lease_t* lease,
                             t5_stream_t* rx, t5_stream_t* tx) {
    if (lease) *lease = 0;
    if (rx) *rx = 0;
    if (tx) *tx = 0;
    if (!request || !lease || !rx || !tx) return T5_SERIAL_INVALID;
    // An orphan private lease has no app-visible handle. It must be retried
    // by end(), never silently displaced by another provider's acquisition.
    if (active_ || releasing_) return T5_SERIAL_BUSY;
    if (generation_ >= 0x7fffffffu) return T5_SERIAL_LIMIT;

    Slot* selected = nullptr;
    bool selectedAvailable = false;
    for (auto& slot : slots_) {
      if (!slot.used) continue;
      const Provider& p = slot.provider;
      if (request->device) {
        if (!p.matches(p.context, request->device)) continue;
        if (selected) return T5_SERIAL_INVALID;
        selected = &slot;
        selectedAvailable = p.available(p.context);
      } else {
        const bool available = p.available(p.context);
        if (available && (!selected || p.priority < selected->provider.priority)) {
          selected = &slot;
          selectedAvailable = true;
        }
      }
    }
    if (!selected) return request->device ? T5_SERIAL_INVALID : T5_SERIAL_UNSUPPORTED;
    if (!selectedAvailable) return T5_SERIAL_UNSUPPORTED;
    const Provider& p = selected->provider;
    t5_serial_port_lease_t privateLease = 0;
    t5_stream_t newRx = 0, newTx = 0;
    const auto result = p.acquire(p.context, request, &privateLease, &newRx, &newTx);
    if (result != T5_SERIAL_OK) {
      // A provider may fail after returning a partial physical acquisition.
      // Its release failure is not proof of cleanup: retain exact ownership.
      if (privateLease) (void)discardOrRetain(*selected, privateLease);
      return result;
    }
    if (!privateLease || !newRx || !newTx || newRx == newTx) {
      if (privateLease) (void)discardOrRetain(*selected, privateLease);
      return T5_SERIAL_IO;
    }
    ++generation_;
    publicLease_ = (generation_ << 1u) | 1u;
    privateLease_ = privateLease;
    active_ = selected;
    orphaned_ = false;
    *lease = publicLease_;
    *rx = newRx;
    *tx = newTx;
    return T5_SERIAL_OK;
  }

  t5_serial_result_t configure(t5_serial_port_lease_t lease, const t5_serial_config_t* config) {
    if (!valid(lease)) return T5_SERIAL_CLOSED;
    const auto& p = active_->provider;
    return p.configure(p.context, privateLease_, config);
  }
  t5_serial_result_t status(t5_serial_port_lease_t lease, t5_serial_port_state_t* state) {
    if (!valid(lease)) return T5_SERIAL_CLOSED;
    const auto& p = active_->provider;
    return p.read_status(p.context, privateLease_, state);
  }
  t5_serial_result_t control(t5_serial_port_lease_t lease, bool dtr, bool rts) {
    if (!valid(lease)) return T5_SERIAL_CLOSED;
    const auto& p = active_->provider;
    return p.set_control_lines(p.context, privateLease_, dtr, rts);
  }
  t5_serial_result_t release(t5_serial_port_lease_t lease) {
    if (!valid(lease)) return T5_SERIAL_CLOSED;
    const Provider p = active_->provider;
    const auto privateLease = privateLease_;
    // Disallow reentry during teardown, but retain a retryable lease if the
    // provider refuses or cannot prove physical quiescence.
    releasing_ = true;
    const auto result = p.release(p.context, privateLease);
    if (result == T5_SERIAL_OK) clearActive();
    releasing_ = false;
    return result;
  }
  void end() {
    if (orphaned_ && active_) {
      // The consumer never received a public lease. Only the registry can
      // retry this exact private handle, including across repeated end().
      releasing_ = true;
      if (active_->provider.release(active_->provider.context, privateLease_) == T5_SERIAL_OK)
        clearActive();
      releasing_ = false;
    } else if (active_) {
      (void)release(publicLease_);
    }
  }
  bool leased() const { return active_ != nullptr; }

 private:
  struct Slot {
    Provider provider{};
    char name[kNameBytes]{};
    bool used = false;
  };
  bool discardOrRetain(Slot& selected, t5_serial_port_lease_t privateLease) {
    releasing_ = true;
    const bool released = selected.provider.release(selected.provider.context, privateLease) ==
                          T5_SERIAL_OK;
    releasing_ = false;
    if (!released) {
      active_ = &selected;
      privateLease_ = privateLease;
      publicLease_ = 0;
      orphaned_ = true;
    }
    return released;
  }
  void clearActive() {
    active_ = nullptr;
    publicLease_ = privateLease_ = 0;
    orphaned_ = false;
  }
  bool valid(t5_serial_port_lease_t lease) const {
    return active_ && !orphaned_ && !releasing_ && lease != 0 && lease == publicLease_;
  }
  Slot slots_[kMaxProviders]{};
  Slot* active_ = nullptr;
  bool releasing_ = false;
  bool orphaned_ = false;
  uint32_t generation_ = 0;
  t5_serial_port_lease_t publicLease_ = 0, privateLease_ = 0;
};
} // namespace RuntimeSerial
