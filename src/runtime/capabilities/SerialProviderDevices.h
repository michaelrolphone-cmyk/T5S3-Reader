#pragma once

#include "ProviderDevicePublisher.h"
#include <RiscSerialPortV1.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

// One installed provider owns its source grant; this hardware-blind adapter
// consumes snapshots synchronously on the invocation-owner task. No USB host,
// descriptors, class policy, ELF pointers or physical transfers enter core.
// Keep the provider ELF/dependencies pinned while any publication is live.
namespace RuntimeDevices {
class SerialProviderDevices final {
 public:
  enum class Result : uint8_t { Updated, Uncertain, Invalid, RegistryFault };
  explicit SerialProviderDevices(Registry& registry)
      : registry_(registry),
        slots_{{registry}, {registry}, {registry}, {registry},
               {registry}, {registry}, {registry}, {registry}} {}
  SerialProviderDevices(const SerialProviderDevices&) = delete;
  SerialProviderDevices& operator=(const SerialProviderDevices&) = delete;

  Result refresh(const char* providerId, const risc_serial_port_api_v1* api) {
    if (!validProvider(providerId) || !api ||
        api->api_version != RISC_SERIAL_PORT_API_V1 ||
        api->struct_size < sizeof(risc_serial_port_inventory_v1))
      return Result::Invalid;
    const auto* source = reinterpret_cast<const risc_serial_port_inventory_v1*>(api);
    if (!source->snapshot || !api->open || !api->close || !api->configure ||
        !api->control_lines || !api->read || !api->write) return Result::Invalid;

    risc_serial_device_v1 observed[RISC_SERIAL_INVENTORY_MAX_DEVICES]{};
    size_t count = RISC_SERIAL_INVENTORY_MAX_DEVICES;
    if (!source->snapshot(observed, &count)) return Result::Uncertain;
    if (count > RISC_SERIAL_INVENTORY_MAX_DEVICES) return Result::Invalid;
    // Validate the COMPLETE snapshot before making even one registry change.
    // A partial/duplicate/stale announcement cannot revoke a working device.
    for (size_t i = 0; i < count; ++i) {
      const auto& record = observed[i];
      if (!record.provider_device || !record.generation ||
          record.transport > RISC_SERIAL_TRANSPORT_IP) return Result::Invalid;
      for (uint8_t reserved : record.reserved)
        if (reserved) return Result::Invalid;
      for (size_t j = 0; j < i; ++j)
        if (record.provider_device == observed[j].provider_device ||
            record.generation == observed[j].generation) return Result::Invalid;
      for (const Slot& existing : slots_)
        if (existing.token == record.provider_device && existing.generation &&
            record.generation < existing.generation) return Result::Invalid;
      char identity[kIdentityBytes]{};
      if (!formatIdentity(identity, providerId, record.provider_device))
        return Result::Invalid;
    }

    // Retire only devices absent from a SUCCESSFUL complete provider snapshot.
    // A transient descriptor/poll failure above preserves all publications.
    for (Slot& slot : slots_) {
      if (!slot.token) continue;
      bool present = false;
      for (size_t i = 0; i < count; ++i)
        if (observed[i].provider_device == slot.token) { present = true; break; }
      if (present) continue;
      if (!slot.publisher.withdraw()) return Result::RegistryFault;
      slot.token = slot.generation = slot.announcement = 0;
    }
    constexpr const char* capabilities[] = {"serial.port"};
    constexpr uint16_t versions[] = {RISC_SERIAL_PORT_API_V1};
    for (size_t i = 0; i < count; ++i) {
      const auto& record = observed[i];
      Slot* slot = nullptr;
      for (Slot& candidate : slots_)
        if (candidate.token == record.provider_device) { slot = &candidate; break; }
      if (!slot) for (Slot& candidate : slots_)
        if (!candidate.token) { slot = &candidate; break; }
      if (!slot) return Result::RegistryFault;
      char identity[kIdentityBytes]{};
      if (!formatIdentity(identity, providerId, record.provider_device))
        return Result::Invalid;
      const Descriptor descriptor{identity, providerId, providerId,
          static_cast<Transport>(record.transport), capabilities, 1, 100, versions};
      const bool changed = slot->token != record.provider_device ||
                           slot->generation != record.generation;
      if (changed && sequence_ == UINT64_MAX) return Result::RegistryFault;
      const uint64_t announcement = changed ? sequence_ + 1u : slot->announcement;
      if (!slot->publisher.publish(announcement, descriptor)) return Result::RegistryFault;
      if (changed) {
        sequence_ = announcement;
        slot->token = record.provider_device;
        slot->generation = record.generation;
        slot->announcement = announcement;
      }
    }
    return Result::Updated;
  }

  // Owner must verify each withdrawal BEFORE releasing its installed provider
  // grant. A failed withdrawal keeps the exact device handle for retry.
  bool withdrawAll() {
    for (Slot& slot : slots_) {
      if (!slot.publisher.withdraw()) return false;
      slot.token = slot.generation = slot.announcement = 0;
    }
    return true;
  }
  DeviceHandle deviceFor(uint64_t providerDevice, uint64_t generation) const {
    for (const Slot& slot : slots_)
      if (slot.token == providerDevice && slot.generation == generation)
        return slot.publisher.device();
    return 0;
  }
  size_t count() const {
    size_t n = 0;
    for (const Slot& slot : slots_) if (slot.token) ++n;
    return n;
  }

 private:
  struct Slot {
    explicit Slot(Registry& registry) : publisher(registry) {}
    ProviderDevicePublisher publisher;
    uint64_t token = 0;
    uint64_t generation = 0;
    uint64_t announcement = 0;
  };
  static bool validProvider(const char* id) {
    if (!id) return false;
    const size_t n = strnlen(id, kProviderBytes);
    if (!n || n == kProviderBytes) return false;
    for (size_t i = 0; i < n; ++i) {
      const unsigned char c = static_cast<unsigned char>(id[i]);
      if (c < 0x21u || c > 0x7eu) return false;
    }
    return true;
  }
  static bool formatIdentity(char (&out)[kIdentityBytes], const char* id,
                             uint64_t token) {
    const int n = std::snprintf(out, sizeof(out), "%s.%016llx", id,
                                static_cast<unsigned long long>(token));
    return n > 0 && static_cast<size_t>(n) < sizeof(out);
  }
  Registry& registry_;
  Slot slots_[RISC_SERIAL_INVENTORY_MAX_DEVICES];
  uint64_t sequence_ = 0;
};
} // namespace RuntimeDevices
