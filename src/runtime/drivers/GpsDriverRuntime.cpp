#include "GpsDriverRuntime.h"
#include "DriverPackage.h"
#include "GpsDriverModule.h"
#include "GpsKernelIo.h"
#include "runtime/capabilities/DeviceRegistry.h"
#include "runtime/resources/ExecutionContext.h"
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
constexpr const char* kCapabilities[] = {
    "location.position", "location.altitude", "location.time",
    "location.accuracy", "location.satellites"};

bool publishDevice(bool packageAvailable) {
  if (!gpsKernelAvailable()) return false;
  auto& registry = RuntimeDevices::systemRegistry();
  RuntimeDevices::DeviceInfo info{};
  if (device && !registry.get(device, &info)) device = 0;
  if (!device) {
    // Stable board binding; the transport is metadata, not a requirement on
    // consumers. The compatibility position.gnss ABI remains inside this adapter.
    const RuntimeDevices::Descriptor desc{
        "board.gnss.uart0", "GNSS", "gps-nmea", RuntimeDevices::Transport::Uart,
        kCapabilities, sizeof(kCapabilities) / sizeof(kCapabilities[0]), 100};
    if (!registry.add(desc, packageAvailable ? RuntimeDevices::State::Available
                                              : RuntimeDevices::State::Unavailable, &device))
      return false;
  } else if (!owner) {
    (void)registry.setState(device, packageAvailable ? RuntimeDevices::State::Available
                                                       : RuntimeDevices::State::Unavailable);
  }
  return packageAvailable;
}

// Firmware callback only. ExecutionContext runs it on the application task
// before unloading the app ELF; stop() releases the provider ELF and hardware.
void cleanupLocation(void*, uint32_t id) {
  if (id == invocation) stop();
  // Even a future provider that aborts without explicitly releasing its token
  // cannot leave an invocation's capability bookkeeping behind.
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
  if (!context->track(RuntimeResources::ExecutionContext::Resource::DeviceLeases,
                      cleanupLocation)) {
    gpsKernelRelease();
    (void)registry.release(positionLease, invocation);
    owner = nullptr;
    positionLease = invocation = 0;
    return false;
  }
  if (module.start(GPS_DRIVER_ELF, host)) {
    LOG_INF("DRIVER", "gps-nmea ACTIVE: location.position (position.gnss compatibility)");
    return true;
  }
  stop();
  (void)registry.setState(device, RuntimeDevices::State::Failed);
  LOG_ERR("DRIVER", "gps-nmea load/start failed");
  return false;
}

void stop() {
  if (!owner || owner != xTaskGetCurrentTaskHandle()) return;
  if (!module.stop()) LOG_ERR("DRIVER", "gps-nmea unload failed; handle retained");
  gpsKernelRelease();
  auto& registry = RuntimeDevices::systemRegistry();
  if (positionLease) (void)registry.release(positionLease, invocation);
  const uint32_t oldInvocation = invocation;
  owner = nullptr;
  positionLease = invocation = 0;
  auto* context = RuntimeResources::ExecutionContext::current();
  if (context && context->id() == oldInvocation) {
    (void)context->untrack(RuntimeResources::ExecutionContext::Resource::DeviceLeases,
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
  if (!owner) return module.read(state);  // Preserve the compatibility OFF state.
  auto* context = RuntimeResources::ExecutionContext::current();
  if (!context || !context->running(invocation) ||
      !RuntimeDevices::systemRegistry().valid(positionLease, invocation)) {
    std::memset(state, 0, sizeof(*state));
    state->status = T5_GPS_STATUS_OFF;
    stop();
    return false;
  }
  const bool ok = module.read(state);
  if (!ok) {
    stop();
    (void)RuntimeDevices::systemRegistry().setState(device, RuntimeDevices::State::Failed);
    LOG_ERR("DRIVER", "gps-nmea provider failed; location lease revoked");
  }
  return ok;
}
}  // namespace GpsDriverRuntime
