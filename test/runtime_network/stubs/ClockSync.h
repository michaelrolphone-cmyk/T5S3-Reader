#pragma once
#include <Arduino.h>
namespace ClockSync { inline void stop() { calls.push_back("clock.stop"); } }
