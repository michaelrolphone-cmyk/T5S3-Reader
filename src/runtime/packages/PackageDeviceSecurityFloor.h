#pragma once

#include "PackageSecurityFloor.h"

#include <cstdint>
#include <mutex>

namespace RuntimePackages {

// Uses ESP-IDF NVS after the firmware has initialized NVS. Not SD storage.
// This is restart/power-interruption persistence, NOT a hardware monotonic
// counter: deployments that allow raw flash rollback must provide a stronger
// backend before claiming rollback resistance against a physical adversary.
class DevicePackageSecurityFloors {
 public:
  FloorRead read(Kind kind, const char* id, uint32_t& floor);
  FloorAdvance advance(Kind kind, const char* id, uint32_t newFloor);

  DevicePackageSecurityFloors(const DevicePackageSecurityFloors&) = delete;
  DevicePackageSecurityFloors& operator=(const DevicePackageSecurityFloors&) = delete;

 private:
  friend DevicePackageSecurityFloors& devicePackageSecurityFloors();
  DevicePackageSecurityFloors() = default;
  std::mutex mutex_;
};

DevicePackageSecurityFloors& devicePackageSecurityFloors();

} // namespace RuntimePackages
