#pragma once
#include <T5DriverApi.h>
#include <T5GnssProvider.h>

// Owns one loaded module. It never gives an application a pointer into an ELF.
// Calls/unload are restricted by GpsDriverRuntime to the claiming app task.
class GpsDriverModule {
 public:
  enum class State { Absent, Loaded, Active, Failed };
  bool start(const char* path, const t5_kernel_io_v1* host);
  bool stop();
  bool read(t5_gps_state_t* state);
  State state() const { return state_; }
 private:
  void* handle_ = nullptr;
  const t5_driver_v1* driver_ = nullptr;
  const t5_gnss_api_v1* gps_ = nullptr;
  State state_ = State::Absent;
};
