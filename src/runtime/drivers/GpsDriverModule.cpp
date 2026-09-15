#include "GpsDriverModule.h"
#include <cstring>
extern "C" {
#include <esp_dlfcn.h>
}

bool GpsDriverModule::start(const char* path, const t5_kernel_io_v1* host) {
  if (state_ == State::Active) return true;
  // A failed dlclose retains the handle; never replace it or run it again.
  if (handle_ || !path || !host) return false;
  state_ = State::Failed;
  (void)dlerror();
  handle_ = dlopen(path, RTLD_NOW);
  if (!handle_) return false;
  state_ = State::Loaded;
  (void)dlerror();
  auto get = reinterpret_cast<t5_driver_get_fn>(dlsym(handle_, "t5_driver_get"));
  const char* error = dlerror();
  if (!error && get) {
    const auto* candidate = get(T5_DRIVER_ABI_VERSION);
    if (candidate && candidate->abi_version == T5_DRIVER_ABI_VERSION &&
        candidate->struct_size >= sizeof(t5_driver_v1) && candidate->driver_id &&
        std::strcmp(candidate->driver_id, "gps-nmea") == 0 && candidate->capability_id &&
        std::strcmp(candidate->capability_id, T5_GNSS_CAPABILITY) == 0 &&
        candidate->capability_api == T5_GNSS_API_VERSION && candidate->capability &&
        candidate->start && candidate->stop) {
      const auto* gps = static_cast<const t5_gnss_api_v1*>(candidate->capability);
      if (gps->api_version == T5_GNSS_API_VERSION && gps->struct_size >= sizeof(*gps) && gps->read) {
        driver_ = candidate;
        gps_ = gps;
        if (driver_->start(host)) {
          state_ = State::Active;
          return true;
        }
      }
    }
  }
  stop();
  state_ = State::Failed;
  return false;
}
bool GpsDriverModule::stop() {
  if (driver_) driver_->stop();
  gps_ = nullptr;
  driver_ = nullptr;
  if (handle_) {
    if (dlclose(handle_) != 0) { state_ = State::Failed; return false; }
    handle_ = nullptr;
  }
  state_ = State::Absent;
  return true;
}
bool GpsDriverModule::read(t5_gps_state_t* state) {
  if (!state) return false;
  std::memset(state, 0, sizeof(*state));
  if (state_ != State::Active || !gps_) { state->status = T5_GPS_STATUS_OFF; return true; }
  if (gps_->read(state)) return true;
  stop();
  state_ = State::Failed;
  std::memset(state, 0, sizeof(*state));
  state->status = T5_GPS_STATUS_OFF;
  return false;
}
