#include <T5AppApi.h>
#include <T5LocationApi.h>
#include "NativeStreamBridge.h"
#include "runtime/capabilities/CapabilityAccess.h"
#include "runtime/drivers/GpsDriverRuntime.h"
#include "runtime/resources/ExecutionContext.h"
#include <cstring>

namespace {
using RuntimeResources::ExecutionContext;

ExecutionContext* caller() {
  auto* context = ExecutionContext::current();
  return context && context->running(context->id()) && t5_app_get_api(T5_APP_ABI_VERSION)
             ? context : nullptr;
}

// Authorization is exact: owner, physical generation, capability NAME and
// READ right. A manifest Dependency is not a trusted hardware source grant;
// an unrelated READ grant must not authorize location.
bool authorizedDevice(t5_device_lease_t authorization, uint32_t* device) {
  if (device) *device = 0;
  auto* context = caller();
  if (!context || !authorization || !device ||
      !RuntimeDevices::systemCapabilityAccess().valid(
          context->id(), authorization, RuntimeDevices::kCapabilityRead, device)) return false;
  RuntimeDevices::LeaseInfo lease{};
  if (!RuntimeDevices::systemRegistry().getLease(authorization, context->id(), &lease) ||
      lease.device != *device || lease.mode != RuntimeDevices::Mode::Dependency ||
      std::strcmp(lease.capability, "location.position") != 0) {
    *device = 0;
    return false;
  }
  return true;
}

t5_stream_result_t subscribe(t5_device_lease_t authorization,
                             t5_location_subscription_t* token, t5_stream_t* stream) {
  if (token) *token = 0;
  if (stream) *stream = 0;
  if (!token || !stream) return T5_STREAM_INVALID;
  uint32_t authorized = 0;
  if (!authorizedDevice(authorization, &authorized)) return T5_STREAM_DENIED;
  auto* context = caller();
  if (!context) return T5_STREAM_DENIED;
  GpsDriverRuntime::LocationSource source{};
  bool startedHere = false;
  if (!GpsDriverRuntime::borrowLocationSource(&source)) {
    if (!GpsDriverRuntime::start()) return T5_STREAM_IO;
    startedHere = true;
    if (!GpsDriverRuntime::borrowLocationSource(&source)) {
      GpsDriverRuntime::stop();
      return T5_STREAM_DISCONNECTED;
    }
  }
  if (source.owner != context->id() || source.device != authorized) {
    if (startedHere) GpsDriverRuntime::stop();
    return T5_STREAM_DENIED;
  }
  const auto result = nativeGnssSubscribe(context->id(), authorized, token, stream);
  if (result != T5_STREAM_OK && startedHere) GpsDriverRuntime::stop();
  return result;
}

t5_stream_result_t poll(t5_device_lease_t authorization) {
  uint32_t authorized = 0;
  if (!authorizedDevice(authorization, &authorized)) return T5_STREAM_DENIED;
  auto* context = caller();
  if (!context) return T5_STREAM_DENIED;
  GpsDriverRuntime::LocationSource source{};
  if (!GpsDriverRuntime::borrowLocationSource(&source) ||
      source.owner != context->id() || source.device != authorized)
    return T5_STREAM_DISCONNECTED;
  t5_gps_state_t copied{};
  // Poll on claiming app task, outside stream mutex; read submits a copy.
  return GpsDriverRuntime::read(&copied) ? T5_STREAM_OK : T5_STREAM_IO;
}

t5_stream_result_t unsubscribe(t5_location_subscription_t token) {
  auto* context = caller();
  if (!context) return T5_STREAM_DENIED;
  return nativeGnssUnsubscribe(context->id(), token);
}
const t5_location_api_v1 api = {
    T5_LOCATION_API_VERSION, sizeof(t5_location_api_v1),
    subscribe, poll, unsubscribe};
}

extern "C" const t5_location_api_v1* t5_location_get_api(uint32_t version) {
  return version == T5_LOCATION_API_VERSION && caller() ? &api : nullptr;
}
