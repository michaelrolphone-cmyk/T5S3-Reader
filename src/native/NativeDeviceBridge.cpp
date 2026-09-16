#include <T5AppApi.h>
#include <T5DeviceApi.h>

#include "NativeDeviceConsent.h"
#include "NativeSerialPortBridge.h"
#include "runtime/capabilities/CapabilityAccess.h"
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
static_assert(kCapabilityRead == T5_DEVICE_RIGHT_READ &&
              kCapabilityWrite == T5_DEVICE_RIGHT_WRITE &&
              kCapabilityConfigure == T5_DEVICE_RIGHT_CONFIGURE, "Rights ABI drift");

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

t5_device_result_t accessResult(AccessResult result) {
  switch (result) {
    case AccessResult::Ok: return T5_DEVICE_OK;
    case AccessResult::Invalid: return T5_DEVICE_INVALID;
    case AccessResult::Denied: return T5_DEVICE_DENIED;
    case AccessResult::Stale: return T5_DEVICE_STALE;
    case AccessResult::Unavailable: return T5_DEVICE_ERR_UNAVAILABLE;
    case AccessResult::Busy: return T5_DEVICE_ERR_BUSY;
    case AccessResult::Limit: return T5_DEVICE_LIMIT;
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

t5_device_result_t acquireCapability(const char* capability, t5_device_handle_t device,
                                     uint32_t rights, t5_device_lease_t* out) {
  if (out) *out = 0;
  if (!out) return T5_DEVICE_INVALID;
  auto* context = caller();
  if (!context) return T5_DEVICE_DENIED;
  nativeDeviceDiscoveryTick();
  return accessResult(systemCapabilityAccess().acquire(*context, capability, device, rights, out));
}

t5_device_result_t validateCapability(t5_device_lease_t lease, uint32_t rights,
                                      t5_device_handle_t* device) {
  if (device) *device = 0;
  auto* context = caller();
  if (!context) return T5_DEVICE_DENIED;
  nativeDeviceDiscoveryTick();
  return systemCapabilityAccess().valid(context->id(), lease, rights, device)
             ? T5_DEVICE_OK : T5_DEVICE_STALE;
}

t5_device_result_t releaseCapability(t5_device_lease_t lease) {
  auto* context = caller();
  if (!context) return T5_DEVICE_DENIED;
  return accessResult(systemCapabilityAccess().release(context->id(), lease));
}

// Only ABI v3 can solicit consent; v2 acquire remains strictly noninteractive.
// The prompt never uses a label supplied by the ELF, and approval is checked
// against a fresh registry snapshot and the same invocation before mutation.
t5_device_result_t requestCapability(const char* capability, t5_device_handle_t device,
                                     uint32_t rights, t5_device_lease_t* out) {
  if (out) *out = 0;
  if (!out || !capability || !device || !rights || (rights & ~kCapabilityRightsMask) ||
      std::strnlen(capability, kCapabilityBytes) == kCapabilityBytes)
    return T5_DEVICE_INVALID;
  auto* context = caller();
  if (!context) return T5_DEVICE_DENIED;
  const uint32_t invocation = context->id();
  nativeDeviceDiscoveryTick();
  auto& access = systemCapabilityAccess();
  const auto previous = access.acquire(*context, capability, device, rights, out);
  if (previous == AccessResult::Ok) return T5_DEVICE_OK;
  if (previous != AccessResult::Denied) return accessResult(previous);

  DeviceInfo before{};
  if (!systemRegistry().get(device, &before)) return T5_DEVICE_STALE;
  if (before.state != State::Available) return T5_DEVICE_ERR_UNAVAILABLE;
  bool supports = false;
  for (size_t i = 0; i < before.capabilityCount; ++i)
    if (std::strcmp(before.capabilities[i], capability) == 0) supports = true;
  if (!supports) return T5_DEVICE_INVALID;
  if (!nativeDeviceConsentPrompt(before, capability, rights)) return T5_DEVICE_DENIED;

  if (caller() != context || context->id() != invocation) return T5_DEVICE_DENIED;
  nativeDeviceDiscoveryTick();
  DeviceInfo after{};
  if (!systemRegistry().get(device, &after)) return T5_DEVICE_STALE;
  if (after.state != State::Available) return T5_DEVICE_ERR_UNAVAILABLE;
  supports = false;
  for (size_t i = 0; i < after.capabilityCount; ++i)
    if (std::strcmp(after.capabilities[i], capability) == 0) supports = true;
  if (!supports || std::strcmp(after.identity, before.identity) != 0) return T5_DEVICE_STALE;
  if (!access.grantTrusted(*context, device, capability, rights)) return T5_DEVICE_LIMIT;
  return accessResult(access.acquire(*context, capability, device, rights, out));
}

const t5_device_api_v1 api = {
    T5_DEVICE_API_VERSION, sizeof(t5_device_api_v1),
    inventory, subscribe, snapshot, poll, unsubscribe};
const t5_device_api_v2 api2 = {
    {T5_DEVICE_API_VERSION_2, sizeof(t5_device_api_v2),
     inventory, subscribe, snapshot, poll, unsubscribe},
    acquireCapability, validateCapability, releaseCapability};
const t5_device_api_v3 api3 = {
    {{T5_DEVICE_API_VERSION_3, sizeof(t5_device_api_v3),
      inventory, subscribe, snapshot, poll, unsubscribe},
     acquireCapability, validateCapability, releaseCapability},
    requestCapability};
}  // namespace

extern "C" const t5_device_api_v1* t5_device_get_api(uint32_t version) {
  if (!caller()) return nullptr;
  if (version == T5_DEVICE_API_VERSION) return &api;
  if (version == T5_DEVICE_API_VERSION_2) return &api2.v1;
  if (version == T5_DEVICE_API_VERSION_3) return &api3.v2.v1;
  return nullptr;
}
