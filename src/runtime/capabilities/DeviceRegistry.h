#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Firmware-only, single-runtime-task registry. Transport callbacks marshal
// changes onto that task. No ELF function pointers, dynamic allocation or
// app-visible implementation pointers are retained.
namespace RuntimeDevices {
constexpr size_t kMaxDevices = 12;
constexpr size_t kMaxLeases = 16;
constexpr size_t kMaxCapabilities = 6;
constexpr size_t kMaxEvents = 24;
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
// Dependency is an invocation-owned, non-authorizing binding: it tracks
// availability and revocation without blocking a provider's physical exclusive
// access. Shared/Exclusive remain the only hardware-access lease modes.
enum class Mode : uint8_t { Shared, Exclusive, Dependency };
enum class Result : uint8_t { Ok, Invalid, NotFound, Unavailable, Busy, Limit, Stale };
struct Descriptor {
  const char* identity = nullptr;
  const char* label = nullptr;
  const char* provider = nullptr;  // Metadata only, never an ELF pointer.
  Transport transport = Transport::Internal;
  const char* const* capabilities = nullptr;
  size_t capabilityCount = 0;
  uint8_t priority = 100;  // Lower is preferred.
  // One API version for each capability; 0 means unknown/unversioned. Existing
  // providers may omit this trailing field but cannot satisfy versioned asks.
  const uint16_t* capabilityApiVersions = nullptr;
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
  uint16_t capabilityApiVersions[kMaxCapabilities]{};
};
struct LeaseInfo {
  LeaseHandle handle = 0;
  DeviceHandle device = 0;
  uint32_t owner = 0;
  Mode mode = Mode::Shared;
  char capability[kCapabilityBytes]{};
};

// A bounded owner-task journal: consumers remember a sequence cursor and poll
// on the same task as registry mutations. No callbacks, task notifications or
// ELF pointers are invoked by a transport callback. Removal retains identity.
enum class EventKind : uint8_t { Added, StateChanged, CapabilityLost, Removed };
enum class PollResult : uint8_t { Next, Empty, Gap, Invalid };
struct Event {
  uint64_t sequence = 0;
  DeviceHandle device = 0;
  EventKind kind = EventKind::Added;
  State previous = State::Discovered;
  State current = State::Discovered;
  uint32_t revokedLeases = 0;
  char identity[kIdentityBytes]{};
};

class Registry final {
 public:
  bool add(const Descriptor& desc, State state, DeviceHandle* out) {
    if (out) *out = 0;
    if (!out || !desc.capabilities || !desc.capabilityCount ||
        desc.capabilityCount > kMaxCapabilities || state == State::Removed) return false;
    DeviceInfo info{};
    if (!copy(info.identity, desc.identity) || !copy(info.label, desc.label, true) ||
        !copy(info.provider, desc.provider)) return false;
    info.state = state;
    info.transport = desc.transport;
    info.priority = desc.priority;
    info.capabilityCount = static_cast<uint8_t>(desc.capabilityCount);
    for (size_t i = 0; i < desc.capabilityCount; ++i) {
      if (!copy(info.capabilities[i], desc.capabilities[i])) return false;
      info.capabilityApiVersions[i] = desc.capabilityApiVersions ? desc.capabilityApiVersions[i] : 0;
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
    publish(EventKind::Added, info, State::Removed, state);
    return true;
  }

  bool setState(DeviceHandle device, State state) {
    if (state == State::Removed) return remove(device);
    Slot* slot = deviceSlot(device);
    if (!slot) return false;
    const State old = slot->info.state;
    if (old == state) return true;
    const bool wasUsable = usable(old);
    const bool nowUsable = usable(state);
    const uint32_t revoked = nowUsable ? 0 : revokeDevice(device);
    slot->info.state = state;
    publish(EventKind::StateChanged, slot->info, old, state, revoked);
    if (wasUsable && !nowUsable)
      publish(EventKind::CapabilityLost, slot->info, old, state, revoked);
    return true;
  }
  bool remove(DeviceHandle device) {
    Slot* slot = deviceSlot(device);
    if (!slot) return false;
    const DeviceInfo previous = slot->info;
    const uint32_t revoked = revokeDevice(device);
    slot->used = false;
    slot->info = DeviceInfo{};
    if (usable(previous.state))
      publish(EventKind::CapabilityLost, previous, previous.state, State::Removed, revoked);
    publish(EventKind::Removed, previous, previous.state, State::Removed, revoked);
    return true;
  }
  bool get(DeviceHandle device, DeviceInfo* out) const {
    if (!out) return false;
    const Slot* slot = deviceSlot(device);
    if (!slot) return false;
    *out = slot->info;
    return true;
  }
  bool at(size_t index, DeviceInfo* out) const {
    if (!out || index >= kMaxDevices || !devices_[index].used) return false;
    *out = devices_[index].info;
    return true;
  }
  size_t count() const {
    size_t n = 0;
    for (const auto& slot : devices_) if (slot.used) ++n;
    return n;
  }

  uint64_t cursor() const { return sequence_; }
  uint64_t overwrittenEvents() const { return overwritten_; }
  PollResult poll(uint64_t* next, Event* out, uint64_t* missed = nullptr) const {
    if (missed) *missed = 0;
    if (!next || !out || *next > sequence_) return PollResult::Invalid;
    const uint64_t oldest = sequence_ >= kMaxEvents ? sequence_ - kMaxEvents + 1u : 1u;
    if (*next < oldest - 1u) {
      if (missed) *missed = oldest - 1u - *next;
      *next = oldest - 1u;
      return PollResult::Gap;
    }
    if (*next == sequence_) return PollResult::Empty;
    const uint64_t wanted = *next + 1u;
    *out = events_[(wanted - 1u) % kMaxEvents];
    *next = wanted;
    return PollResult::Next;
  }

  Result acquire(const char* capability, uint32_t owner, LeaseHandle* out,
                 DeviceHandle preferred = 0, Mode mode = Mode::Shared) {
    if (out) *out = 0;
    if (!out || !owner || !validText(capability, kCapabilityBytes)) return Result::Invalid;
    if (preferred && !deviceSlot(preferred)) return Result::NotFound;
    Slot* best = nullptr;
    bool offline = false, busy = false;
    for (auto& slot : devices_) {
      if (!slot.used || (preferred && slot.info.handle != preferred)) continue;
      bool supports = false;
      for (size_t i = 0; i < slot.info.capabilityCount; ++i) {
        if (std::strcmp(capability, slot.info.capabilities[i]) == 0) {
          supports = true;
          break;
        }
      }
      if (!supports) continue;
      if (slot.info.state == State::Busy) { busy = true; continue; }
      if (slot.info.state != State::Available) { offline = true; continue; }
      bool conflicts = false;
      if (mode != Mode::Dependency) {
        for (const auto& lease : leases_) {
          if (lease.used && lease.info.device == slot.info.handle &&
              lease.info.mode != Mode::Dependency &&
              (mode == Mode::Exclusive || lease.info.mode == Mode::Exclusive)) {
            conflicts = true;
            break;
          }
        }
      }
      if (conflicts) { busy = true; continue; }
      if (!best || slot.info.priority < best->info.priority) best = &slot;
    }
    if (!best) return busy ? Result::Busy : (offline ? Result::Unavailable : Result::NotFound);
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
    if (!slot || !owner || slot->info.owner != owner) return false;
    const Slot* device = deviceSlot(slot->info.device);
    return device && usable(device->info.state);
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
  static bool usable(State state) {
    return state == State::Available || state == State::Busy;
  }
  void publish(EventKind kind, const DeviceInfo& device, State previous, State current,
               uint32_t revoked = 0) {
    if (sequence_ == UINT64_MAX) return;
    Event event{};
    event.sequence = ++sequence_;
    event.device = device.handle;
    event.kind = kind;
    event.previous = previous;
    event.current = current;
    event.revokedLeases = revoked;
    std::strcpy(event.identity, device.identity);
    events_[(sequence_ - 1u) % kMaxEvents] = event;
    if (sequence_ > kMaxEvents && overwritten_ != UINT64_MAX) ++overwritten_;
  }
  static uint32_t makeHandle(size_t index, uint32_t generation) {
    return (generation << 8u) | static_cast<uint32_t>(index + 1u);
  }
  static bool validText(const char* src, size_t capacity, bool spaces = false) {
    if (!src) return false;
    size_t i = 0;
    for (; i < capacity && src[i]; ++i) {
      const unsigned char ch = static_cast<unsigned char>(src[i]);
      if (ch < (spaces ? 0x20u : 0x21u) || ch > 0x7eu) return false;
    }
    return i && i < capacity;
  }
  template <size_t N> static bool copy(char (&out)[N], const char* src,
                                        bool spaces = false) {
    if (!validText(src, N, spaces)) return false;
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
  uint32_t revokeDevice(DeviceHandle device) {
    uint32_t revoked = 0;
    for (auto& lease : leases_) {
      if (lease.used && lease.info.device == device) {
        lease.used = false;
        lease.info = LeaseInfo{};
        ++revoked;
      }
    }
    return revoked;
  }
  Slot devices_[kMaxDevices]{};
  LeaseSlot leases_[kMaxLeases]{};
  Event events_[kMaxEvents]{};
  uint64_t sequence_ = 0;
  uint64_t overwritten_ = 0;
};

inline Registry& systemRegistry() {
  static Registry registry;
  return registry;
}
}  // namespace RuntimeDevices
