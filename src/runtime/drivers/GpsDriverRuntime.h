#pragma once
#include <T5GpsApi.h>
namespace GpsDriverRuntime {
bool available();
bool start();
void stop();
bool read(t5_gps_state_t* state);
}
