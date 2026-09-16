#include <T5AppApi.h>
#include <T5DeviceApi.h>

#include "NativeSerialPortBridge.h"
#include "runtime/capabilities/DeviceEventSubscriptions.h"
#include "runtime/resources/ExecutionContext.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {
using RuntimeResources::ExecutionContext;
using namespace RuntimeDevices;

static_assert(kMaxCapabilities == T5_DEVICE_CAPABILITY_COUNT, "Device ABI capabilities drift");
static_assert(kIdentityBytes == T5_DEVICE_IDENTITY_MAX, "Device ABI identity drift");
static_assert(kLabelBytes == T5_DEVICE_LABEL_MAX, "Device ABI label drift");
static_assert(kProviderBytes == T5_DEVICE_PROVIDER_MAX, "Device ABI provider drift");
static_assert(kCapabilityBytes == T5_DEVICE_CAPABILITY_MAX, "Device ABI capability drift");
static_assert(static_cast<uint8_t>(Transport::Ip) == T5_DEVICE_TRANSPORT_IP, "Transport ABI drift");
static_assert(static_cast<uint8_t>(State::Failed) == T5_DEVICE_FAILED, "State ABI drift");
static_assert(static_cast<uint8_t>(EventKind::Removed) == T5_DEVICE_REMOVAL, "Event ABI drift");

ExecutionContext* caller() {
  auto* context = ExecutionContext::current();
  if (!context || !context->running(context->id()) ||
      !t5_app_get_api(T5_APP_ABI_VERSION)) return nullptr;
  return context;
}

void copyInfo(t5_device_info_t& dest, const DeviceInfo& source) {
  dest = {};
  dest.handle = source.handle;
  dest.state = static_cast<uint8_t>(source.state);
  dest.transport = static_cast<uint8_t>(source.transport);
  dest.priority = source.priority;
  dest.capability_count = source.capabilityCount;
  std::memcpy(dest.identity, source.identity, sizeof(dest.identity));
  std::memcpy(dest.label, source.label, sizeof(dest.label));
  std::memcpy(dest.provider, source.provider, sizeof(dest.provider));
  std::memcpy(dest.capabilities, source.capabilities, sizeof(dest.capabilities));
}

void copyEvent(t5_device_event_t& dest, const Event& source) {
  dest = {};
  dest.sequence = source.sequence;
  dest.device = source.device;
  dest.kind = static_cast<uint8_t>(source.kind);
  dest.previous = static_cast<uint8_t>(source.previous);
  dest.current = static_cast<uint8_t>(source.current);
  dest.revoked_leases = source.revokedLeases;
  std::memcpy(dest.identity, source.identity, sizeof(dest.identity));
}

t5_device_result_t observationResult(ObserveResult result) {
  switch (result) {
    case ObserveResult::Ok: return T5_DEVICE_OK;
    case ObserveResult::Next: return T5_DEVICE_NEXT;
    case ObserveResult::Empty: return T5_DEVICE_EMPTY;
    case ObserveResult::Gap: return T5_DEVICE_GAP;
    case ObserveResult::Invalid: return T5_DEVICE_INVALID;
    case ObserveResult::Denied: return T5_DEVICE_DENIED;
    case ObserveResult::Stale: return T5_DEVICE_STALE;
    case ObserveResult::Limit: return T5_DEVICE_LIMIT;
  }
  return T5_DEVICE_INVALID;
}

t5_device_result_t inventory(t5_device_info_t* out, uint32_t capacity, uint32_t* count) {
  if (count) *count = 0;
  if (!count || (capacity && !out)) return T5_DEVICE_INVALID;
  if (!caller()) return T5_DEVICE_DENIED;
  nativeDeviceDiscoveryTick();
  auto& registry = systemRegistry();
  const size_t required = registry.count();
  *count = static_cast<uint32_t>(required);
  if (capacity < required) return T5_DEVICE_LIMIT;
  size_t written = 0;
  for (size_t index = 0; index < kMaxDevices; ++index) {
    DeviceInfo info{};
    if (registry.at(index, &info)) copyInfo(out[written++], info);
  }
  return T5_DEVICE_OK;
}

t5_device_result_t subscribe(t5_device_subscription_t* out) {
  if (out) *out = 0;
  if (!out) return T5_DEVICE_INVALID;
  auto* context = caller();
  if (!context) return T5_DEVICE_DENIED;
  nativeDeviceDiscoveryTick();
  SubscriptionHandle next = 0;
  const auto result = systemEventSubscriptions().subscribe(*context, &next);
  if (result == ObserveResult::Ok) *out = next;
  return observationResult(result);
}

t5_device_result_t snapshot(t5_device_subscription_t subscription, t5_device_info_t* out,
                            uint32_t capacity, uint32_t* count) {
  if (count) *count = 0;
  if (!count || (capacity && !out)) return T5_DEVICE_INVALID;
  auto* context = caller();
  if (!context) return T5_DEVICE_DENIED;
  nativeDeviceDiscoveryTick();
  // Firmware-owned fixed scratch: snapshot() validates the complete buffer
  // capacity BEFORE acknowledging an event gap or moving the cursor. No ELF
  // pointer is retained by the registry or its subscriber table.
  static DeviceInfo scratch[kMaxDevices]{};
  size_t needed = 0;
  const auto result = systemEventSubscriptions().snapshot(
      subscription, context->id(), scratch, capacity < kMaxDevices ? capacity : kMaxDevices, &needed);
  *count = static_cast<uint32_t>(needed);
  if (result == ObserveResult::Ok) {
    for (size_t index = 0; index < needed; ++index) copyInfo(out[index], scratch[index]);
  }
  return observationResult(result);
}

t5_device_result_t poll(t5_device_subscription_t subscription, t5_device_event_t* out,
                        uint64_t* missed) {
  if (out) *out = {};
  if (missed) *missed = 0;
  if (!out) return T5_DEVICE_INVALID;
  auto* context = caller();
  if (!context) return T5_DEVICE_DENIED;
  nativeDeviceDiscoveryTick();
  Event event{};
  const auto result = systemEventSubscriptions().poll(subscription, context->id(), &event, missed);
  if (result == ObserveResult::Next) copyEvent(*out, event);
  return observationResult(result);
}

t5_device_result_t unsubscribe(t5_device_subscription_t subscription) {
  auto* context = caller();
  if (!context) return T5_DEVICE_DENIED;
  return observationResult(systemEventSubscriptions().unsubscribe(subscription, context->id()));
}

const t5_device_api_v1 api = {
    T5_DEVICE_API_VERSION, sizeof(t5_device_api_v1),
    inventory, subscribe, snapshot, poll, unsubscribe};
}  // namespace

extern "C" const t5_device_api_v1* t5_device_get_api(uint32_t version) {
  return version == T5_DEVICE_API_VERSION && caller() ? &api : nullptr;
}
