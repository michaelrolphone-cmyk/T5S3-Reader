#include <T5AppApi.h>
#include <T5GpsApi.h>
#include "runtime/drivers/GpsDriverRuntime.h"

// Compatibility facade: native apps retain their API and never hold a pointer
// into a loadable provider. Receiver logic now exists only in the driver ELF.
namespace {
bool active() { return t5_app_get_api(T5_APP_ABI_VERSION) != nullptr; }
bool supported() { return active() && GpsDriverRuntime::available(); }
bool start() { return active() && GpsDriverRuntime::start(); }
void stop() { if (active()) GpsDriverRuntime::stop(); }
bool read(t5_gps_state_t* state) { return active() && GpsDriverRuntime::read(state); }
const t5_gps_api_v1 api = {T5_GPS_API_VERSION, sizeof(t5_gps_api_v1), supported, start, stop, read};
}
extern "C" const t5_gps_api_v1* t5_gps_get_api(uint32_t version) {
  return version == T5_GPS_API_VERSION && active() ? &api : nullptr;
}
