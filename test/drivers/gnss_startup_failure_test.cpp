#include "runtime/drivers/GpsDriverRuntime.h"
#include "runtime/drivers/GpsDriverModule.h"
#include "runtime/drivers/GpsKernelIo.h"
#include "runtime/drivers/DriverPackage.h"
#include "runtime/capabilities/DeviceRegistry.h"
#include "runtime/resources/ExecutionContext.h"
#include "native/NativeStreamBridge.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using RuntimeResources::ExecutionContext;
namespace {
bool kernelClaimed = false;
bool moduleActivates = true;
t5_stream_result_t attachResult = T5_STREAM_BUSY;
unsigned kernelClaims = 0, kernelReleases = 0;
unsigned moduleStarts = 0, moduleStops = 0, moduleReads = 0;
unsigned streamAttaches = 0, streamDisconnects = 0, streamPublishes = 0;
}

bool validateGpsDriverPackage() { return true; }
bool gpsKernelAvailable() { return true; }
const t5_kernel_io_v1* gpsKernelClaim() {
  assert(!kernelClaimed);
  static const t5_kernel_io_v1 host{};
  kernelClaimed = true;
  ++kernelClaims;
  return &host;
}
void gpsKernelRelease() {
  assert(kernelClaimed);
  kernelClaimed = false;
  ++kernelReleases;
}
bool GpsDriverModule::start(const char* path, const t5_kernel_io_v1* host) {
  assert(path && std::strcmp(path, GPS_DRIVER_ELF) == 0 && host);
  ++moduleStarts;
  state_ = moduleActivates ? State::Active : State::Absent;
  return true;
}
bool GpsDriverModule::stop() {
  ++moduleStops;
  state_ = State::Absent;
  return true;
}
bool GpsDriverModule::read(t5_gps_state_t* state) {
  assert(state && state_ == State::Active);
  ++moduleReads;
  *state = {};
  state->status = T5_GPS_STATUS_FIX;
  state->fix_valid = 1;
  state->latitude = 44.5;
  state->longitude = -116.0;
  return true;
}

t5_stream_result_t nativeGnssAttach(uint32_t owner, uint32_t device, uint32_t borrowedLease) {
  auto* context = ExecutionContext::current();
  assert(context && context->running(owner) && device && borrowedLease &&
         RuntimeDevices::systemRegistry().valid(borrowedLease, owner));
  ++streamAttaches;
  return attachResult;
}
void nativeGnssDisconnect(uint32_t owner) {
  auto* context = ExecutionContext::current();
  assert(context && context->id() == owner);
  ++streamDisconnects;
}
RuntimeStreams::LiveGnssSession::PollDecision nativeGnssBeforePoll(uint32_t owner) {
  assert(ExecutionContext::current()->running(owner));
  return RuntimeStreams::LiveGnssSession::PollDecision::Poll;
}
t5_stream_result_t nativeGnssPublishCopy(uint32_t owner, const t5_gps_state_t& state,
                                        uint32_t sampleMs) {
  assert(ExecutionContext::current()->running(owner));
  assert(state.fix_valid && sampleMs == 1000u);
  ++streamPublishes;
  return T5_STREAM_OK;
}

int main() {
  ExecutionContext context;
  auto& registry = RuntimeDevices::systemRegistry();
  assert(context.begin());
  const uint32_t owner = context.id();
  assert(GpsDriverRuntime::available());

  // Hardware and driver are usable, but the runtime stream registry refuses
  // attachment (for example, older undrained subscribers or exhausted slots).
  assert(!GpsDriverRuntime::start());
  assert(streamAttaches == 1 && moduleStarts == 1 && moduleStops == 1);
  assert(kernelClaims == 1 && kernelReleases == 1 && !kernelClaimed);
  assert(registry.leaseCount() == 0);
  GpsDriverRuntime::LocationSource source{9, 9, 9};
  assert(!GpsDriverRuntime::borrowLocationSource(&source) &&
         source.owner == 0 && source.device == 0 && source.lease == 0);

  // A failed attachment must not mark the physical receiver Failed: after
  // the stream resources are freed, this invocation can retry successfully.
  attachResult = T5_STREAM_OK;
  assert(GpsDriverRuntime::start());
  assert(streamAttaches == 2 && moduleStarts == 2 && kernelClaims == 2);
  assert(GpsDriverRuntime::borrowLocationSource(&source));
  assert(source.owner == owner && registry.valid(source.lease, owner));
  assert(GpsDriverRuntime::start() && streamAttaches == 2); // Idempotent.
  t5_gps_state_t observation{};
  assert(GpsDriverRuntime::read(&observation) && moduleReads == 1 && streamPublishes == 1);
  GpsDriverRuntime::stop();
  assert(streamDisconnects == 1 && moduleStops == 2 && kernelReleases == 2);
  assert(registry.leaseCount() == 0 && !kernelClaimed);

  // Even if a malformed module reports start success without ACTIVE state,
  // borrowing its source must fail closed and must release the UART claim.
  moduleActivates = false;
  assert(!GpsDriverRuntime::start());
  assert(streamAttaches == 2 && moduleStops == 3 && kernelReleases == 3);
  assert(registry.leaseCount() == 0 && !kernelClaimed);
  moduleActivates = true;

  // A normal active provider is also cleaned on exceptional app-context exit.
  assert(GpsDriverRuntime::start());
  assert(registry.leaseCount() == 1 && kernelClaimed);
  context.end();
  assert(moduleStops == 4 && streamDisconnects == 2 && kernelReleases == 4);
  assert(registry.leaseCount() == 0 && !kernelClaimed);
  assert(ExecutionContext::current() == nullptr);
  std::puts("GNSS runtime fails closed on stream attach failure and cleans up on retry/exit");
}
