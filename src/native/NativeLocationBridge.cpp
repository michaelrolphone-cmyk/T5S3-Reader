#include <T5AppApi.h>
#include <T5LocationApi.h>
#include "NativeStreamBridge.h"
#include "runtime/capabilities/ProviderAuthorization.h"
#include "runtime/drivers/GpsDriverRuntime.h"
#include "runtime/resources/ExecutionContext.h"

namespace {
using RuntimeResources::ExecutionContext;

ExecutionContext* caller() {
  auto* context = ExecutionContext::current();
  return context && context->running(context->id()) && t5_app_get_api(T5_APP_ABI_VERSION)
             ? context : nullptr;
}

// Derive the expected physical generation from the firmware registry rather
// than trusting an app-provided handle. ProviderAuthorization verifies that
// this exact lease was ISSUED by consent, is still live for this invocation,
// names location.position and includes READ. A plain manifest dependency or
// an unrelated location grant cannot authorize a GNSS data operation.
bool authorizedDevice(t5_device_lease_t authorization, uint32_t* device) {
  if (device) *device = 0;
  auto* context = caller();
  if (!context || !authorization || !device) return false;
  auto& registry = RuntimeDevices::systemRegistry();
  RuntimeDevices::LeaseInfo lease{};
  if (!registry.getLease(authorization, context->id(), &lease) ||
      !RuntimeDevices::ProviderAuthorization::semantic(
          RuntimeDevices::systemCapabilityAccess(), registry, *context,
          authorization, lease.device, "location.position", RuntimeDevices::kCapabilityRead))
    return false;
  *device = lease.device;
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
  // Driver startup may yield; approval is never cached across it. Verify
  // permission and the same physical generation before handing back a stream.
  uint32_t rechecked = 0;
  if (source.owner != context->id() || source.device != authorized ||
      !authorizedDevice(authorization, &rechecked) || rechecked != authorized) {
    if (startedHere) GpsDriverRuntime::stop();
    return T5_STREAM_DENIED;
  }
  const auto result = nativeGnssSubscribe(context->id(), authorized, token, stream);
  if (result != T5_STREAM_OK) {
    if (startedHere) GpsDriverRuntime::stop();
    return result;
  }
  if (!authorizedDevice(authorization, &rechecked) || rechecked != authorized) {
    (void)nativeGnssUnsubscribe(context->id(), *token);
    *token = 0;
    *stream = 0;
    if (startedHere) GpsDriverRuntime::stop();
    return T5_STREAM_DENIED;
  }
  return T5_STREAM_OK;
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
