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
t5_stream_result_t publishResult = T5_STREAM_OK;
RuntimeStreams::LiveGnssSession::PollDecision pollDecision =
    RuntimeStreams::LiveGnssSession::PollDecision::Poll;
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
  return pollDecision;
}
t5_stream_result_t nativeGnssPublishCopy(uint32_t owner, const t5_gps_state_t& state,
                                        uint32_t sampleMs) {
  assert(ExecutionContext::current()->running(owner));
  assert(state.fix_valid && sampleMs == 1000u);
  ++streamPublishes;
  return publishResult;
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
  // borrowing its source must fail closed and release the UART claim.
  moduleActivates = false;
  assert(!GpsDriverRuntime::start());
  assert(streamAttaches == 2 && moduleStops == 3 && kernelReleases == 3);
  assert(registry.leaseCount() == 0 && !kernelClaimed);
  moduleActivates = true;

  // A source may be revoked during preflight OR the record registry may fail
  // after the ELF read. Neither path may return a fix or leave an active UART
  // that subsequently serves legacy raw reads without a semantic stream.
  auto failRead = [&](RuntimeStreams::LiveGnssSession::PollDecision decision,
                      t5_stream_result_t publication) {
    pollDecision = RuntimeStreams::LiveGnssSession::PollDecision::Poll;
    publishResult = T5_STREAM_OK;
    assert(GpsDriverRuntime::start());
    assert(GpsDriverRuntime::available());
    const unsigned reads = moduleReads, publishes = streamPublishes;
    const unsigned stops = moduleStops, disconnects = streamDisconnects;
    const unsigned releases = kernelReleases;
    pollDecision = decision;
    publishResult = publication;
    t5_gps_state_t value{};
    value.latitude = 81.0; // Must be cleared even after a successful ELF read.
    assert(!GpsDriverRuntime::read(&value));
    assert(value.status == T5_GPS_STATUS_OFF && !value.fix_valid &&
           value.latitude == 0 && value.longitude == 0);
    const bool readHardware = decision == RuntimeStreams::LiveGnssSession::PollDecision::Poll;
    assert(moduleReads == reads + (readHardware ? 1u : 0u));
    assert(streamPublishes == publishes + (readHardware ? 1u : 0u));
    assert(moduleStops == stops + 1 && streamDisconnects == disconnects + 1);
    assert(kernelReleases == releases + 1 && !kernelClaimed);
    assert(registry.leaseCount() == 0);
    GpsDriverRuntime::LocationSource stale{1, 1, 1};
    assert(!GpsDriverRuntime::borrowLocationSource(&stale) && !stale.owner);
    pollDecision = RuntimeStreams::LiveGnssSession::PollDecision::Poll;
    publishResult = T5_STREAM_OK;
  };
  failRead(RuntimeStreams::LiveGnssSession::PollDecision::Disconnected, T5_STREAM_OK);
  failRead(RuntimeStreams::LiveGnssSession::PollDecision::Denied, T5_STREAM_OK);
  failRead(RuntimeStreams::LiveGnssSession::PollDecision::Poll, T5_STREAM_DISCONNECTED);
  failRead(RuntimeStreams::LiveGnssSession::PollDecision::Poll, T5_STREAM_DENIED);
  failRead(RuntimeStreams::LiveGnssSession::PollDecision::Poll, T5_STREAM_BUSY);

  // Backpressure and retry are not fatal: they must return cached data and
  // must NEVER trigger an additional UART read or record publication.
  assert(GpsDriverRuntime::start());
  assert(GpsDriverRuntime::read(&observation));
  const unsigned reads = moduleReads, publishes = streamPublishes;
  pollDecision = RuntimeStreams::LiveGnssSession::PollDecision::Backpressured;
  t5_gps_state_t cached{};
  assert(GpsDriverRuntime::read(&cached) && cached.latitude == observation.latitude);
  pollDecision = RuntimeStreams::LiveGnssSession::PollDecision::Retried;
  assert(GpsDriverRuntime::read(&cached) && cached.longitude == observation.longitude);
  assert(moduleReads == reads && streamPublishes == publishes);
  pollDecision = RuntimeStreams::LiveGnssSession::PollDecision::Poll;

  // A normal active provider is also cleaned on exceptional app-context exit.
  assert(registry.leaseCount() == 1 && kernelClaimed);
  const unsigned stops = moduleStops, disconnects = streamDisconnects;
  const unsigned releases = kernelReleases;
  context.end();
  assert(moduleStops == stops + 1 && streamDisconnects == disconnects + 1 &&
         kernelReleases == releases + 1);
  assert(registry.leaseCount() == 0 && !kernelClaimed);
  assert(ExecutionContext::current() == nullptr);
  std::puts("GNSS startup, poll, publish, retry and context teardown fail-closed tests passed");
}
