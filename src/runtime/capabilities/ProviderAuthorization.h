#pragma once

#include "CapabilityAccess.h"
#include "DeviceRegistry.h"
#include "runtime/resources/ExecutionContext.h"
#include <cstdint>
#include <cstring>

// Firmware-only provider I/O gate. A consent-derived authorization and a
// physical device reservation are distinct credentials. Neither a manifest
// dependency, a forged handle, nor a physical lease alone authorizes I/O.
// Providers invoke this immediately before their actual hardware operation;
// never cache a positive result across device removal or app exit.
namespace RuntimeDevices {

class ProviderAuthorization final {
 public:
  // For semantic read-only services which own their source elsewhere (GNSS).
  // expectedDevice is an exact generation-qualified Registry handle, NOT a
  // transport-independent model name or an app-provided device preference.
  static bool semantic(const CapabilityAccess& access, const Registry& devices,
                       const RuntimeResources::ExecutionContext& context,
                       LeaseHandle authorization, DeviceHandle expectedDevice,
                       const char* expectedCapability, uint32_t rights) {
    if (!caller(context) || !expectedDevice || !expectedCapability || !rights ||
        (rights & ~kCapabilityRightsMask)) return false;
    DeviceHandle resolved = 0;
    if (!access.valid(context.id(), authorization, rights, &resolved) ||
        resolved != expectedDevice) return false;
    LeaseInfo issued{};
    return devices.getLease(authorization, context.id(), &issued) &&
           issued.device == expectedDevice && issued.owner == context.id() &&
           issued.mode == Mode::Dependency &&
           std::strcmp(issued.capability, expectedCapability) == 0;
  }

  // For transport I/O the provider must ALSO hold a physical Shared/Exclusive
  // Registry lease for the exact same capability, device and invocation.
  // A dependency-only lease is never a physical reservation. The resource
  // manager remains responsible for shared/exclusive conflict resolution.
  static bool io(const CapabilityAccess& access, const Registry& devices,
                 const RuntimeResources::ExecutionContext& context,
                 LeaseHandle authorization, LeaseHandle physical,
                 DeviceHandle expectedDevice, const char* expectedCapability,
                 uint32_t rights, Mode requiredMode = Mode::Exclusive) {
    if (!semantic(access, devices, context, authorization, expectedDevice,
                  expectedCapability, rights) || !physical ||
        requiredMode == Mode::Dependency) return false;
    LeaseInfo source{};
    return devices.getLease(physical, context.id(), &source) &&
           source.owner == context.id() && source.device == expectedDevice &&
           source.mode == requiredMode &&
           std::strcmp(source.capability, expectedCapability) == 0;
  }

 private:
  static bool caller(const RuntimeResources::ExecutionContext& context) {
    return RuntimeResources::ExecutionContext::current() == &context &&
           context.running(context.id());
  }
};

}  // namespace RuntimeDevices
