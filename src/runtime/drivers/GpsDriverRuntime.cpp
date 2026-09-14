#include "GpsDriverRuntime.h"
#include "DriverPackage.h"
#include "GpsDriverModule.h"
#include "GpsKernelIo.h"
#include <Logging.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
namespace GpsDriverRuntime {
namespace {
GpsDriverModule module;
TaskHandle_t owner = nullptr;
}
bool available() { return gpsKernelAvailable() && validateGpsDriverPackage(); }
bool start() {
  if (owner) return owner == xTaskGetCurrentTaskHandle() && module.state() == GpsDriverModule::State::Active;
  if (!available()) return false;
  const auto* host = gpsKernelClaim();
  if (!host) return false;
  owner = xTaskGetCurrentTaskHandle();
  if (module.start(GPS_DRIVER_ELF, host)) {
    LOG_INF("DRIVER", "gps-nmea ACTIVE: position.gnss API 1");
    return true;
  }
  gpsKernelRelease();
  owner = nullptr;
  LOG_ERR("DRIVER", "gps-nmea load/start failed");
  return false;
}
void stop() {
  if (owner && owner != xTaskGetCurrentTaskHandle()) return;
  if (!module.stop()) LOG_ERR("DRIVER", "gps-nmea unload failed; handle retained");
  gpsKernelRelease();
  owner = nullptr;
}
bool read(t5_gps_state_t* state) {
  if (!state) return false;
  if (owner && owner != xTaskGetCurrentTaskHandle()) return false;
  if (!gpsKernelAvailable()) {
    std::memset(state, 0, sizeof(*state)); state->status = T5_GPS_STATUS_UNSUPPORTED; return true;
  }
  const bool ok = module.read(state);
  if (!ok) {
    gpsKernelRelease();
    owner = nullptr;
    LOG_ERR("DRIVER", "gps-nmea provider failed; resources released");
  }
  return ok;
}
}
