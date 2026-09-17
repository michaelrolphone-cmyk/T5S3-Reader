#pragma once
#include <T5GpsApi.h>
#include <cstdint>

namespace GpsDriverRuntime {
// Firmware-only identity for an already active GNSS provider. The lease is
// borrowed: callers MUST NOT release it or retain it across stop()/unload.
// Neither this type nor its accessor is exported to application ELFs.
struct LocationSource {
  uint32_t owner = 0;
  uint32_t device = 0;
  uint32_t lease = 0;
};
bool available();
bool start();
void stop();
bool read(t5_gps_state_t* state);
// Returns false, and zeros out, for an inactive, foreign-task, stale or
// revoked provider. Only the active invocation's owning task can borrow it.
bool borrowLocationSource(LocationSource* out);
}
