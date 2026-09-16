#pragma once

#include "runtime/capabilities/DeviceRegistry.h"
#include <T5GpsApi.h>
#include <T5SerialPortApi.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace RuntimeDevices {

// Manifest metadata is copied into firmware memory, never retained as pointers
// into an application ELF. Legacy manifests have count == 0.
constexpr size_t kMaxAppRequirements = 6;
struct AppCapabilityRequirement {
  char capability[kCapabilityBytes]{};
  uint16_t minApi = 0;
};
struct AppCapabilityRequirements {
  AppCapabilityRequirement entries[kMaxAppRequirements]{};
  size_t count = 0;
};

inline bool validCapabilityName(const char* name) {
  if (!name || name[0] < 'a' || name[0] > 'z') return false;
  size_t n = 0;
  for (; n < kCapabilityBytes && name[n]; ++n) {
    const char c = name[n];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
          c == '.' || c == '_' || c == '-')) return false;
  }
  return n > 0 && n < kCapabilityBytes;
}

// The initial manifest syntax is an explicit minimum major API: ">=1".
// Versions not recorded by a provider cannot be silently assumed compatible.
inline bool parseMinimumApi(const char* text, uint16_t* out) {
  if (out) *out = 0;
  if (!out || !text || text[0] != '>' || text[1] != '=' ||
      text[2] < '1' || text[2] > '9') return false;
  uint32_t version = 0;
  for (const char* p = text + 2; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
    version = version * 10u + static_cast<uint32_t>(*p - '0');
    if (version > UINT16_MAX) return false;
  }
  *out = static_cast<uint16_t>(version);
  return true;
}

inline bool addRequirement(AppCapabilityRequirements* requirements,
                           const char* capability, const char* api) {
  if (!requirements || requirements->count >= kMaxAppRequirements ||
      !validCapabilityName(capability)) return false;
  uint16_t version = 0;
  if (!parseMinimumApi(api, &version)) return false;
  for (size_t i = 0; i < requirements->count; ++i)
    if (std::strcmp(requirements->entries[i].capability, capability) == 0) return false;
  auto& entry = requirements->entries[requirements->count++];
  std::strcpy(entry.capability, capability);  // Name length validated above.
  entry.minApi = version;
  return true;
}

enum class RequirementResult : uint8_t {
  Ready, Missing, Unavailable, UnknownApi, ApiTooOld
};

// Version knowledge is intentionally limited to *existing, versioned host
// facades*. A provider's name alone is not permission to open that hardware.
// Additional capabilities require registered API-version metadata before they
// can pass a mandatory manifest requirement. No device lease is acquired here.
inline uint16_t knownApiVersion(const DeviceInfo& device, const char* capability) {
  if (std::strcmp(device.provider, "usb.serial") == 0 &&
      std::strcmp(capability, "serial.port") == 0)
    return static_cast<uint16_t>(T5_SERIAL_PORT_API_VERSION);
  if (std::strcmp(device.provider, "gps-nmea") == 0 &&
      std::strcmp(capability, "location.position") == 0)
    return static_cast<uint16_t>(T5_GPS_API_VERSION);
  return 0;
}

inline RequirementResult resolveRequirement(const Registry& registry,
                                            const AppCapabilityRequirement& requested) {
  bool seen = false, unversioned = false, older = false;
  for (size_t slot = 0; slot < kMaxDevices; ++slot) {
    DeviceInfo device{};
    if (!registry.at(slot, &device)) continue;
    bool supports = false;
    for (size_t i = 0; i < device.capabilityCount; ++i)
      if (std::strcmp(device.capabilities[i], requested.capability) == 0) {
        supports = true;
        break;
      }
    if (!supports) continue;
    seen = true;
    if (device.state != State::Available) continue;
    const uint16_t version = knownApiVersion(device, requested.capability);
    if (!version) { unversioned = true; continue; }
    if (version < requested.minApi) { older = true; continue; }
    return RequirementResult::Ready;
  }
  if (!seen) return RequirementResult::Missing;
  if (older) return RequirementResult::ApiTooOld;
  if (unversioned) return RequirementResult::UnknownApi;
  return RequirementResult::Unavailable;
}

inline RequirementResult resolveRequirements(const Registry& registry,
                                              const AppCapabilityRequirements& requested,
                                              size_t* failingIndex = nullptr) {
  if (failingIndex) *failingIndex = requested.count;
  if (requested.count > kMaxAppRequirements) return RequirementResult::Missing;
  for (size_t i = 0; i < requested.count; ++i) {
    const RequirementResult result = resolveRequirement(registry, requested.entries[i]);
    if (result != RequirementResult::Ready) {
      if (failingIndex) *failingIndex = i;
      return result;
    }
  }
  return RequirementResult::Ready;
}

}  // namespace RuntimeDevices
