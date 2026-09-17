#include "GpsDriverRuntime.h"
#include "DriverPackage.h"
#include "GpsDriverModule.h"
#include "GpsKernelIo.h"
#include "native/NativeStreamBridge.h"
#include "runtime/capabilities/DeviceRegistry.h"
#include "runtime/resources/ExecutionContext.h"
#include <Arduino.h>
#include <Logging.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>

namespace GpsDriverRuntime {
namespace {
GpsDriverModule module;
TaskHandle_t owner = nullptr;
RuntimeDevices::DeviceHandle device = 0;
RuntimeDevices::LeaseHandle positionLease = 0;
uint32_t invocation = 0;
bool gnssBound = false;
t5_gps_state_t lastObservation{};
constexpr const char* kCapabilities[] = {
    "location.position", "location.altitude", "location.time",
    "location.accuracy", "location.satellites"};
// Only location.position has a currently implemented versioned ELF facade.
constexpr uint16_t kCapabilityApiVersions[] = {T5_GPS_API_VERSION, 0, 0, 0, 0};

bool publishDevice(bool packageAvailable) {
  if (!gpsKernelAvailable()) return false;
  auto& registry = RuntimeDevices::systemRegistry();
  RuntimeDevices::DeviceInfo info{};
  if (device && !registry.get(device, &info)) device = 0;
  if (!device) {
    const RuntimeDevices::Descriptor desc{
        "board.gnss.uart0", "GNSS", "gps-nmea", RuntimeDevices::Transport::Uart,
        kCapabilities, sizeof(kCapabilities) / sizeof(kCapabilities[0]), 100,
        kCapabilityApiVersions};
    if (!registry.add(desc, packageAvailable ? RuntimeDevices::State::Available
                                              : RuntimeDevices::State::Unavailable, &device))
      return false;
  } else if (!owner) {
    (void)registry.setState(device, packageAvailable ? RuntimeDevices::State::Available
                                                       : RuntimeDevices::State::Unavailable);
  }
  return packageAvailable;
}

void cleanupLocation(void*, uint32_t id) {
  if (id == invocation) stop();
  (void)RuntimeDevices::systemRegistry().releaseOwner(id);
}
}  // namespace

bool available() {
  if (owner) return module.state() == GpsDriverModule::State::Active &&
                    RuntimeDevices::systemRegistry().valid(positionLease, invocation);
  return publishDevice(gpsKernelAvailable() && validateGpsDriverPackage());
}

bool start() {
  auto* context = RuntimeResources::ExecutionContext::current();
  if (!context || !context->running(context->id())) return false;
  if (owner) {
    return owner == xTaskGetCurrentTaskHandle() && invocation == context->id() &&
           module.state() == GpsDriverModule::State::Active &&
           RuntimeDevices::systemRegistry().valid(positionLease, invocation);
  }
  if (!available()) return false;
  auto& registry = RuntimeDevices::systemRegistry();
  RuntimeDevices::LeaseHandle newLease = 0;
  if (registry.acquire("location.position", context->id(), &newLease, device) !=
      RuntimeDevices::Result::Ok) return false;
  const auto* host = gpsKernelClaim();
  if (!host) {
    (void)registry.release(newLease, context->id());
    return false;
  }
  owner = xTaskGetCurrentTaskHandle();
  invocation = context->id();
  positionLease = newLease;
  gnssBound = false;
  lastObservation = {};
  // CapabilityAccess independently tracks DeviceLeases when the user grants
  // location READ. ExecutionContext permits only one handler per Resource,
  // so the driver must have a distinct slot and release before ELF unload.
  if (!context->track(RuntimeResources::ExecutionContext::Resource::GnssDriver,
                      cleanupLocation)) {
    gpsKernelRelease();
    (void)registry.release(positionLease, invocation);
    owner = nullptr;
    positionLease = invocation = 0;
    return false;
  }
  if (module.start(GPS_DRIVER_ELF, host)) {
    LocationSource source{};
    if (borrowLocationSource(&source)) {
      const auto result = nativeGnssAttach(source.owner, source.device, source.lease);
      gnssBound = result == T5_STREAM_OK;
      if (!gnssBound) LOG_ERR("DRIVER", "location.fix.v1 stream attach failed: %ld", static_cast<long>(result));
    }
    LOG_INF("DRIVER", "gps-nmea ACTIVE: location.position (position.gnss compatibility)");
    return true;
  }
  stop();
  (void)registry.setState(device, RuntimeDevices::State::Failed);
  LOG_ERR("DRIVER", "gps-nmea load/start failed");
  return false;
}

bool borrowLocationSource(LocationSource* out) {
  if (out) *out = {};
  if (!out || !owner || owner != xTaskGetCurrentTaskHandle() || !invocation || !device || !positionLease ||
      module.state() != GpsDriverModule::State::Active) return false;
  auto* context = RuntimeResources::ExecutionContext::current();
  if (!context || !context->running(invocation) || context->id() != invocation) return false;
  auto& registry = RuntimeDevices::systemRegistry();
  RuntimeDevices::LeaseInfo info{};
  if (!registry.valid(positionLease, invocation) || !registry.getLease(positionLease, invocation, &info) ||
      info.owner != invocation || info.device != device ||
      std::strcmp(info.capability, "location.position") != 0 ||
      info.mode == RuntimeDevices::Mode::Dependency) return false;
  *out = {invocation, device, positionLease};
  return true;
}

void stop() {
  if (!owner || owner != xTaskGetCurrentTaskHandle()) return;
  // Finish queued records and clear any backpressured sample BEFORE dlclose,
  // UART release, or release of the driver's borrowed source grant.
  if (gnssBound) nativeGnssDisconnect(invocation);
  gnssBound = false;
  lastObservation = {};
  if (!module.stop()) LOG_ERR("DRIVER", "gps-nmea unload failed; handle retained");
  gpsKernelRelease();
  auto& registry = RuntimeDevices::systemRegistry();
  if (positionLease) (void)registry.release(positionLease, invocation);
  const uint32_t oldInvocation = invocation;
  owner = nullptr;
  positionLease = invocation = 0;
  auto* context = RuntimeResources::ExecutionContext::current();
  if (context && context->id() == oldInvocation) {
    (void)context->untrack(RuntimeResources::ExecutionContext::Resource::GnssDriver,
                           oldInvocation);
  }
}

bool read(t5_gps_state_t* state) {
  if (!state) return false;
  if (owner && owner != xTaskGetCurrentTaskHandle()) return false;
  if (!gpsKernelAvailable()) {
    std::memset(state, 0, sizeof(*state));
    state->status = T5_GPS_STATUS_UNSUPPORTED;
    return true;
  }
  if (!owner) return module.read(state);
  auto* context = RuntimeResources::ExecutionContext::current();
  if (!context || !context->running(invocation) ||
      !RuntimeDevices::systemRegistry().valid(positionLease, invocation)) {
    std::memset(state, 0, sizeof(*state));
    state->status = T5_GPS_STATUS_OFF;
    stop();
    return false;
  }
  if (gnssBound) {
    const auto decision = nativeGnssBeforePoll(invocation);
    if (decision == RuntimeStreams::LiveGnssSession::PollDecision::Backpressured ||
        decision == RuntimeStreams::LiveGnssSession::PollDecision::Retried) {
      *state = lastObservation; // Never poll/overwrite a pending observation.
      return true;
    }
    if (decision != RuntimeStreams::LiveGnssSession::PollDecision::Poll) {
      nativeGnssDisconnect(invocation);
      gnssBound = false;
    }
  }
  // The synchronous ELF driver is called ONLY on its claiming task and OUTSIDE
  // the stream mutex. The bridge copies this observation before publishing.
  const bool ok = module.read(state);
  if (!ok) {
    stop();
    (void)RuntimeDevices::systemRegistry().setState(device, RuntimeDevices::State::Failed);
    LOG_ERR("DRIVER", "gps-nmea provider failed; location lease revoked");
    return false;
  }
  lastObservation = *state;
  if (gnssBound) {
    const auto result = nativeGnssPublishCopy(invocation, lastObservation, millis());
    if (result == T5_STREAM_DISCONNECTED) {
      nativeGnssDisconnect(invocation);
      gnssBound = false;
    }
  }
  return true;
}
}  // namespace GpsDriverRuntime
