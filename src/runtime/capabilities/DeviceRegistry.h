#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Firmware-only, single-runtime-task registry. Transport callbacks must marshal
// changes onto that task. No ELF pointers, dynamic allocation, or app-visible
// pointers are retained. Public application access requires a separate host API
// that authenticates the calling execution context.
namespace RuntimeDevices {
constexpr size_t kMaxDevices = 12;
constexpr size_t kMaxLeases = 16;
constexpr size_t kMaxCapabilities = 6;
constexpr size_t kIdentityBytes = 48;
constexpr size_t kLabelBytes = 48;
constexpr size_t kProviderBytes = 32;
constexpr size_t kCapabilityBytes = 40;
using DeviceHandle = uint32_t;
using LeaseHandle = uint32_t;

enum class Transport : uint8_t { Internal, Uart, Usb, Ble, I2c, Spi, Gpio, Lora, Ip };
enum class State : uint8_t {
  Discovered, Identified, Bound, Available, Busy, Suspended, Unavailable, Removed, Failed
};
enum class Mode : uint8_t { Shared, Exclusive };
enum class Result : uint8_t { Ok, Invalid, NotFound, Unavailable, Busy, Limit, Stale };

struct Descriptor {
  const char* identity = nullptr;   // Unique stable logical identity, never an ephemeral USB address.
  const char* label = nullptr;
  const char* provider = nullptr;   // Informational runtime-owned provider identity, NOT an ELF pointer.
  Transport transport = Transport::Internal;
  const char* const* capabilities = nullptr;
  size_t capabilityCount = 0;
  uint8_t priority = 100;           // Lower number wins unconstrained resolution.
};
struct DeviceInfo {
  DeviceHandle handle = 0;
  State state = State::Discovered;
  Transport transport = Transport::Internal;
  uint8_t priority = 100;
  uint8_t capabilityCount = 0;
  char identity[kIdentityBytes]{};
  char label[kLabelBytes]{};
  char provider[kProviderBytes]{};
  char capabilities[kMaxCapabilities][kCapabilityBytes]{};
};
struct LeaseInfo {
  LeaseHandle handle = 0;
  DeviceHandle device = 0;
  uint32_t owner = 0;
  Mode mode = Mode::Shared;
  char capability[kCapabilityBytes]{};
};

class Registry final {
 public:
  bool add(const Descriptor& desc, State state, DeviceHandle* out) {
    if (out) *out = 0;
    if (!out || !desc.capabilities || !desc.capabilityCount ||
        desc.capabilityCount > kMaxCapabilities || state == State::Removed) return false;
    DeviceInfo info{};
    if (!copy(info.identity, desc.identity) || !copy(info.label, desc.label) ||
        !copy(info.provider, desc.provider)) return false;
    info.state = state;
    info.transport = desc.transport;
    info.priority = desc.priority;
    info.capabilityCount = static_cast<uint8_t>(desc.capabilityCount);
    for (size_t i = 0; i < desc.capabilityCount; ++i) {
      if (!copy(info.capabilities[i], desc.capabilities[i])) return false;
      for (size_t j = 0; j < i; ++j) {
        if (std::strcmp(info.capabilities[i], info.capabilities[j]) == 0) return false;
      }
    }
    Slot* empty = nullptr;
    for (auto& slot : devices_) {
      if (slot.used && std::strcmp(slot.info.identity, info.identity) == 0) return false;
      if (!slot.used && slot.generation < kMaxGeneration && !empty) empty = &slot;
    }
    if (!empty) return false;
    ++empty->generation;
    info.handle = makeHandle(static_cast<size_t>(empty - devices_), empty->generation);
    empty->info = info;
    empty->used = true;
    *out = info.handle;
    return true;
  }

  bool setState(DeviceHandle device, State state) {
    Slot* slot = deviceSlot(device);
    if (!slot) return false;
    if (state != State::Available) revokeDevice(device);
    slot->info.state = state;
    return true;
  }

  bool remove(DeviceHandle device) {
    Slot* slot = deviceSlot(device);
    if (!slot) return false;
    revokeDevice(device);
    slot->used = false;  // Preserve generation so old handles cannot alias reuse.
    slot->info = DeviceInfo{};
    return true;
  }

  bool get(DeviceHandle device, DeviceInfo* out) const {
    if (!out) return false;
    const Slot* slot = deviceSlot(device);
    if (!slot) return false;
    *out = slot->info;
    return true;
  }
  // index is a physical slot, not a compact ordinal: stable while other slots change.
  bool at(size_t index, DeviceInfo* out) const {
    if (!out || index >= kMaxDevices || !devices_[index].used) return false;
    *out = devices_[index].info;
    return true;
  }
  size_t count() const {
    size_t total = 0;
    for (const auto& slot : devices_) if (slot.used) ++total;
    return total;
  }

  Result acquire(const char* capability, uint32_t owner, LeaseHandle* out,
                 DeviceHandle preferred = 0, Mode mode = Mode::Shared) {
    if (out) *out = 0;
    if (!out || !owner || !validName(capability, kCapabilityBytes)) return Result::Invalid;
    if (preferred && !deviceSlot(preferred)) return Result::NotFound;
    Slot* best = nullptr;
    bool foundCapability = false, offline = false, busy = false;
    for (auto& slot : devices_) {
      if (!slot.used || (preferred && slot.info.handle != preferred)) continue;
      bool supports = false;
      for (size_t i = 0; i < slot.info.capabilityCount; ++i) {
        if (std::strcmp(capability, slot.info.capabilities[i]) == 0) { supports = true; break; }
      }
      if (!supports) continue;
      foundCapability = true;
      if (slot.info.state != State::Available) { offline = true; continue; }
      bool conflicts = false;
      for (const auto& lease : leases_) {
        if (lease.used && lease.info.device == slot.info.handle &&
            (mode == Mode::Exclusive || lease.info.mode == Mode::Exclusive)) {
          conflicts = true;
          break;
        }
      }
      if (conflicts) { busy = true; continue; }
      if (!best || slot.info.priority < best->info.priority) best = &slot;
    }
    if (!best) {
      if (busy) return Result::Busy;
      if (offline) return Result::Unavailable;
      return foundCapability ? Result::Unavailable : Result::NotFound;
    }
    for (size_t i = 0; i < kMaxLeases; ++i) {
      LeaseSlot& slot = leases_[i];
      if (slot.used || slot.generation >= kMaxGeneration) continue;
      ++slot.generation;
      slot.used = true;
      slot.info = LeaseInfo{};
      slot.info.handle = makeHandle(i, slot.generation);
      slot.info.device = best->info.handle;
      slot.info.owner = owner;
      slot.info.mode = mode;
      (void)copy(slot.info.capability, capability);
      *out = slot.info.handle;
      return Result::Ok;
    }
    return Result::Limit;
  }

  bool valid(LeaseHandle lease, uint32_t owner) const {
    const LeaseSlot* slot = leaseSlot(lease);
    return slot && slot->info.owner == owner && deviceSlot(slot->info.device) &&
           deviceSlot(slot->info.device)->info.state == State::Available;
  }
  bool getLease(LeaseHandle lease, uint32_t owner, LeaseInfo* out) const {
    if (!out || !valid(lease, owner)) return false;
    *out = leaseSlot(lease)->info;
    return true;
  }
  Result release(LeaseHandle lease, uint32_t owner) {
    LeaseSlot* slot = leaseSlot(lease);
    if (!slot || !owner || slot->info.owner != owner) return Result::Stale;
    slot->used = false;
    slot->info = LeaseInfo{};
    return Result::Ok;
  }
  size_t releaseOwner(uint32_t owner) {
    if (!owner) return 0;
    size_t count = 0;
    for (auto& slot : leases_) {
      if (!slot.used || slot.info.owner != owner) continue;
      slot.used = false;
      slot.info = LeaseInfo{};
      ++count;
    }
    return count;
  }
  size_t leaseCount() const {
    size_t count = 0;
    for (const auto& slot : leases_) if (slot.used) ++count;
    return count;
  }

 private:
  static constexpr uint32_t kMaxGeneration = 0x00ffffffu;
  struct Slot { DeviceInfo info{}; uint32_t generation = 0; bool used = false; };
  struct LeaseSlot { LeaseInfo info{}; uint32_t generation = 0; bool used = false; };
  static DeviceHandle makeHandle(size_t index, uint32_t generation) {
    return (generation << 8u) | static_cast<uint32_t>(index + 1u);
  }
  static bool validName(const char* src, size_t capacity) {
    if (!src) return false;
    size_t i = 0;
    for (; i < capacity && src[i]; ++i) {
      if (static_cast<unsigned char>(src[i]) < 0x21u ||
          static_cast<unsigned char>(src[i]) > 0x7eu) return false;
    }
    return i && i < capacity;
  }
  template <size_t N> static bool copy(char (&out)[N], const char* src) {
    if (!validName(src, N)) return false;
    std::strcpy(out, src);
    return true;
  }
  Slot* deviceSlot(DeviceHandle handle) {
    const uint32_t low = handle & 0xffu;
    if (!low || low > kMaxDevices) return nullptr;
    Slot& slot = devices_[low - 1u];
    return slot.used && slot.generation == (handle >> 8u) ? &slot : nullptr;
  }
  const Slot* deviceSlot(DeviceHandle handle) const {
    return const_cast<Registry*>(this)->deviceSlot(handle);
  }
  LeaseSlot* leaseSlot(LeaseHandle handle) {
    const uint32_t low = handle & 0xffu;
    if (!low || low > kMaxLeases) return nullptr;
    LeaseSlot& slot = leases_[low - 1u];
    return slot.used && slot.generation == (handle >> 8u) ? &slot : nullptr;
  }
  const LeaseSlot* leaseSlot(LeaseHandle handle) const {
    return const_cast<Registry*>(this)->leaseSlot(handle);
  }
  void revokeDevice(DeviceHandle device) {
    for (auto& lease : leases_) {
      if (!lease.used || lease.info.device != device) continue;
      lease.used = false;
      lease.info = LeaseInfo{};
    }
  }
  Slot devices_[kMaxDevices]{};
  LeaseSlot leases_[kMaxLeases]{};
};

// A single firmware-owned inventory shared by all transport adapters.
inline Registry& systemRegistry() {
  static Registry registry;
  return registry;
}
}  // namespace RuntimeDevices
