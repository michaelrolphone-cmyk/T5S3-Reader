#include <T5AppApi.h>
#include <T5LocationApi.h>
#include "native/NativeStreamBridge.h"
#include "runtime/capabilities/CapabilityAccess.h"
#include "runtime/drivers/GpsDriverRuntime.h"
#include "runtime/resources/ExecutionContext.h"
#include <cassert>
#include <cstdio>

using namespace RuntimeDevices;
using RuntimeResources::ExecutionContext;

namespace {
static bool appApiEnabled = true;
static bool providerActive = false;
static DeviceHandle receiver = 0;
static LeaseHandle sourceGrant = 0;
static uint32_t polls = 0, subscribes = 0, unsubscribes = 0;
static uint32_t sourceOwner = 0;
}

extern "C" const t5_app_api_v1* t5_app_get_api(uint32_t version) {
  static const t5_app_api_v1 app{};
  return appApiEnabled && version == T5_APP_ABI_VERSION ? &app : nullptr;
}

namespace GpsDriverRuntime {
bool available() { return true; }
bool start() {
  auto* context = ExecutionContext::current();
  if (!context || !context->running(context->id())) return false;
  sourceOwner = context->id();
  if (!sourceGrant && systemRegistry().acquire("location.position", sourceOwner,
                                               &sourceGrant, receiver) != Result::Ok) return false;
  providerActive = true;
  return true;
}
void stop() {
  if (sourceGrant) (void)systemRegistry().release(sourceGrant, sourceOwner);
  sourceGrant = 0;
  sourceOwner = 0;
  providerActive = false;
}
bool borrowLocationSource(LocationSource* out) {
  if (out) *out = {};
  auto* context = ExecutionContext::current();
  if (!out || !providerActive || !context || !context->running(sourceOwner) ||
      !systemRegistry().valid(sourceGrant, sourceOwner)) return false;
  *out = {sourceOwner, receiver, sourceGrant};
  return true;
}
bool read(t5_gps_state_t* state) {
  if (!state || !providerActive) return false;
  *state = {};
  state->status = T5_GPS_STATUS_FIX;
  state->fix_valid = 1;
  state->latitude = 44.5;
  state->longitude = -116.0;
  ++polls;
  return true;
}
}

t5_stream_result_t nativeGnssSubscribe(uint32_t owner, uint32_t device,
                                      uint64_t* token, t5_stream_t* stream) {
  auto* context = ExecutionContext::current();
  if (!context || owner != context->id() || device != receiver || !token || !stream)
    return T5_STREAM_DENIED;
  *token = 0x100000001ull;
  *stream = 0x101u;
  ++subscribes;
  return T5_STREAM_OK;
}
t5_stream_result_t nativeGnssUnsubscribe(uint32_t owner, uint64_t token) {
  auto* context = ExecutionContext::current();
  if (!context || owner != context->id() || token != 0x100000001ull) return T5_STREAM_DENIED;
  ++unsubscribes;
  return T5_STREAM_OK;
}

int main() {
  assert(t5_location_get_api(T5_LOCATION_API_VERSION) == nullptr);
  constexpr const char* capabilities[] = {"location.position", "location.altitude"};
  const Descriptor desc{"board.gnss.uart0", "GNSS", "gps-nmea", Transport::Uart,
                        capabilities, 2, 100};
  assert(systemRegistry().add(desc, State::Available, &receiver));
  ExecutionContext context;
  assert(context.begin());
  auto* api = t5_location_get_api(T5_LOCATION_API_VERSION);
  assert(api && api->api_version == T5_LOCATION_API_VERSION &&
         api->struct_size == sizeof(t5_location_api_v1));
  assert(t5_location_get_api(T5_LOCATION_API_VERSION + 1) == nullptr);
  t5_location_subscription_t token = 99;
  t5_stream_t stream = 99;
  assert(api->subscribe(0, &token, &stream) == T5_STREAM_DENIED && !token && !stream);
  // A raw physical source lease is not a capability authorization.
  LeaseHandle physical = 0;
  assert(systemRegistry().acquire("location.position", context.id(), &physical, receiver) == Result::Ok);
  assert(api->subscribe(physical, &token, &stream) == T5_STREAM_DENIED);
  assert(systemRegistry().release(physical, context.id()) == Result::Ok);
  // A manifest dependency is non-authorizing even when it has the correct name.
  LeaseHandle dependency = 0;
  assert(systemRegistry().acquire("location.position", context.id(), &dependency,
                                  receiver, Mode::Dependency) == Result::Ok);
  assert(api->subscribe(dependency, &token, &stream) == T5_STREAM_DENIED);
  assert(systemRegistry().release(dependency, context.id()) == Result::Ok);
  // A trusted grant for another location capability must not authorize position.
  auto& access = systemCapabilityAccess();
  assert(access.grantTrusted(context, receiver, "location.altitude", kCapabilityRead));
  LeaseHandle altitude = 0;
  assert(access.acquire(context, "location.altitude", receiver, kCapabilityRead,
                        &altitude) == AccessResult::Ok);
  assert(api->subscribe(altitude, &token, &stream) == T5_STREAM_DENIED);
  assert(access.grantTrusted(context, receiver, "location.position", kCapabilityRead));
  LeaseHandle permission = 0;
  assert(access.acquire(context, "location.position", receiver, kCapabilityRead,
                        &permission) == AccessResult::Ok);
  assert(api->subscribe(permission, &token, &stream) == T5_STREAM_OK);
  assert(token && stream && subscribes == 1 && providerActive);
  assert(api->poll(permission) == T5_STREAM_OK && polls == 1);
  assert(api->unsubscribe(token) == T5_STREAM_OK && unsubscribes == 1);
  assert(access.release(context.id(), permission) == AccessResult::Ok);
  assert(api->poll(permission) == T5_STREAM_DENIED && polls == 1);
  // A device generation replacement must invalidate the old authorization.
  assert(systemRegistry().remove(receiver));
  assert(api->subscribe(altitude, &token, &stream) == T5_STREAM_DENIED);
  GpsDriverRuntime::stop();
  context.end();
  assert(systemRegistry().leaseCount() == 0 && access.owner() == 0);
  assert(t5_location_get_api(T5_LOCATION_API_VERSION) == nullptr);
  assert(context.begin());
  assert(t5_location_get_api(T5_LOCATION_API_VERSION)->poll(permission) == T5_STREAM_DENIED);
  context.end();
  std::puts("Native GNSS bridge authorization and invocation lifecycle tests passed");
}
