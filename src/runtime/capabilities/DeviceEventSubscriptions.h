#pragma once

#include "DeviceRegistry.h"
#include "runtime/resources/ExecutionContext.h"
#include <cstddef>
#include <cstdint>

// Firmware-only, owner-task subscriptions over the device event journal.
// This is NOT an ELF ABI and must not be invoked by a transport callback or
// stream worker. Subscribers have independent cursors, generation-safe handles
// and execution-context cleanup before their application ELF is unloaded.
namespace RuntimeDevices {
constexpr size_t kMaxDeviceSubscriptions = 8;
using SubscriptionHandle = uint32_t;
enum class ObserveResult : uint8_t {
  Ok, Next, Empty, Gap, Invalid, Denied, Stale, Limit
};

class EventSubscriptions final {
 public:
  explicit EventSubscriptions(Registry& registry) : registry_(registry) {}

  ObserveResult subscribe(RuntimeResources::ExecutionContext& context,
                          SubscriptionHandle* out) {
    if (out) *out = 0;
    if (!out) return ObserveResult::Invalid;
    if (RuntimeResources::ExecutionContext::current() != &context ||
        !context.running(context.id())) return ObserveResult::Denied;
    Slot* empty = nullptr;
    bool tracked = false;
    for (auto& slot : slots_) {
      if (slot.used && slot.owner == context.id()) tracked = true;
      if (!slot.used && slot.generation < kMaxGeneration && !empty) empty = &slot;
    }
    if (!empty) return ObserveResult::Limit;
    if (!tracked && !context.track(RuntimeResources::ExecutionContext::Resource::DeviceEvents,
                                    cleanup, this)) return ObserveResult::Limit;
    ++empty->generation;
    empty->used = true;
    empty->owner = context.id();
    empty->cursor = registry_.cursor();  // Subscribing never replays another app's history.
    empty->needsSnapshot = false;
    *out = makeHandle(static_cast<size_t>(empty - slots_), empty->generation);
    return ObserveResult::Ok;
  }

  // A gap forces an atomic inventory snapshot before any further event reads.
  // The owner task copies the inventory and resets its cursor in one call;
  // insufficient buffer capacity never acknowledges the gap or loses events.
  ObserveResult snapshot(SubscriptionHandle handle, uint32_t owner,
                         DeviceInfo* out, size_t capacity, size_t* count) {
    if (count) *count = 0;
    if (!count || (capacity && !out)) return ObserveResult::Invalid;
    const ObserveResult access = authorize(handle, owner);
    if (access != ObserveResult::Ok) return access;
    Slot* slot = find(handle);
    const size_t required = registry_.count();
    *count = required;
    if (capacity < required) return ObserveResult::Limit;
    size_t written = 0;
    for (size_t i = 0; i < kMaxDevices; ++i) {
      DeviceInfo info{};
      if (registry_.at(i, &info)) out[written++] = info;
    }
    slot->cursor = registry_.cursor();
    slot->needsSnapshot = false;
    return ObserveResult::Ok;
  }

  ObserveResult poll(SubscriptionHandle handle, uint32_t owner, Event* out,
                     uint64_t* missed = nullptr) {
    if (missed) *missed = 0;
    if (!out) return ObserveResult::Invalid;
    const ObserveResult access = authorize(handle, owner);
    if (access != ObserveResult::Ok) return access;
    Slot* slot = find(handle);
    if (slot->needsSnapshot) return ObserveResult::Gap;
    const PollResult result = registry_.poll(&slot->cursor, out, missed);
    switch (result) {
      case PollResult::Next: return ObserveResult::Next;
      case PollResult::Empty: return ObserveResult::Empty;
      case PollResult::Gap:
        slot->needsSnapshot = true;
        return ObserveResult::Gap;
      case PollResult::Invalid: return ObserveResult::Invalid;
    }
    return ObserveResult::Invalid;
  }

  ObserveResult unsubscribe(SubscriptionHandle handle, uint32_t owner) {
    const ObserveResult access = authorize(handle, owner);
    if (access != ObserveResult::Ok) return access;
    Slot* slot = find(handle);
    slot->used = false;
    slot->owner = 0;
    slot->cursor = 0;
    slot->needsSnapshot = false;
    bool remaining = false;
    for (const auto& candidate : slots_)
      if (candidate.used && candidate.owner == owner) remaining = true;
    if (!remaining) {
      auto* context = RuntimeResources::ExecutionContext::current();
      (void)context->untrack(RuntimeResources::ExecutionContext::Resource::DeviceEvents, owner);
    }
    return ObserveResult::Ok;
  }

  size_t count() const {
    size_t result = 0;
    for (const auto& slot : slots_) if (slot.used) ++result;
    return result;
  }

 private:
  static constexpr uint32_t kMaxGeneration = 0x00ffffffu;
  struct Slot {
    uint32_t generation = 0;
    uint32_t owner = 0;
    uint64_t cursor = 0;
    bool used = false;
    bool needsSnapshot = false;
  };
  static SubscriptionHandle makeHandle(size_t index, uint32_t generation) {
    return (generation << 8u) | static_cast<uint32_t>(index + 1u);
  }
  Slot* find(SubscriptionHandle handle) {
    const uint32_t low = handle & 0xffu;
    if (!low || low > kMaxDeviceSubscriptions) return nullptr;
    Slot& slot = slots_[low - 1u];
    return slot.used && slot.generation == handle >> 8u ? &slot : nullptr;
  }
  ObserveResult authorize(SubscriptionHandle handle, uint32_t owner) {
    auto* context = RuntimeResources::ExecutionContext::current();
    if (!owner || !context || !context->running(owner)) return ObserveResult::Denied;
    Slot* slot = find(handle);
    if (!slot) return ObserveResult::Stale;
    return slot->owner == owner ? ObserveResult::Ok : ObserveResult::Denied;
  }
  static void cleanup(void* opaque, uint32_t owner) {
    static_cast<EventSubscriptions*>(opaque)->releaseOwner(owner);
  }
  void releaseOwner(uint32_t owner) {
    for (auto& slot : slots_) {
      if (!slot.used || slot.owner != owner) continue;
      slot.used = false;
      slot.owner = 0;
      slot.cursor = 0;
      slot.needsSnapshot = false;
    }
  }
  Registry& registry_;
  Slot slots_[kMaxDeviceSubscriptions]{};
};

inline EventSubscriptions& systemEventSubscriptions() {
  static EventSubscriptions subscriptions(systemRegistry());
  return subscriptions;
}
}  // namespace RuntimeDevices
