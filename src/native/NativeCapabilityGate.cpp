#include "AppManifest.h"
#include "NativeSerialPortBridge.h"
#include "runtime/capabilities/AppCapabilityRequirements.h"
#include "runtime/drivers/GpsDriverRuntime.h"

#include <HalStorage.h>
#include <esp_log.h>

#include <cstring>
#include <string>

namespace {
constexpr const char* kTag = "capability_gate";
const char* reason(RuntimeDevices::RequirementResult result) {
  switch (result) {
    case RuntimeDevices::RequirementResult::Ready: return "ready";
    case RuntimeDevices::RequirementResult::Missing: return "not discovered";
    case RuntimeDevices::RequirementResult::Unavailable: return "not available";
    case RuntimeDevices::RequirementResult::UnknownApi: return "no declared provider API version";
    case RuntimeDevices::RequirementResult::ApiTooOld: return "provider API too old";
  }
  return "invalid";
}
}  // namespace

// Trusted firmware-only launch hook called on the owner task before dlopen.
// This is a launch-time compatibility check, NEVER a permission grant or
// substitute for obtaining invocation-owned capability leases inside providers.
extern "C" bool native_app_capabilities_ready(const char* sd_path) {
  if (!sd_path || std::strncmp(sd_path, "/sd/", 4) != 0) return false;
  const std::string elf(sd_path + 3);  // VFS /sd/Foo -> HalStorage /Foo.
  if (elf.size() < 5 || elf.compare(elf.size() - 4, 4, ".elf") != 0) return false;
  const std::string sidecar = elf.substr(0, elf.size() - 4) + ".json";
  if (!Storage.exists(sidecar.c_str())) return true;  // Legacy loose ELFs.

  t5_app_manifest_t manifest{};
  RuntimeDevices::AppCapabilityRequirements requirements{};
  if (!readAppManifest(sidecar.c_str(), manifest, nullptr, false, &requirements) ||
      !manifest.compatible ||
      elf.substr(elf.find_last_of('/') + 1) != manifest.file_name) {
    ESP_LOGE(kTag, "Invalid, incompatible or mismatched application sidecar: %s", sidecar.c_str());
    return false;
  }
  if (!requirements.count) return true;

  // Existing GNSS binding is lazy; publishing its availability probes the
  // package/hardware without starting UART or loading the driver ELF.
  for (size_t i = 0; i < requirements.count; ++i)
    if (std::strcmp(requirements.entries[i].capability, "location.position") == 0) {
      (void)GpsDriverRuntime::available();
      break;
    }
  nativeDeviceDiscoveryTick();
  size_t failed = 0;
  const auto result = RuntimeDevices::resolveRequirements(
      RuntimeDevices::systemRegistry(), requirements, &failed);
  if (result == RuntimeDevices::RequirementResult::Ready) return true;
  if (failed < requirements.count)
    ESP_LOGE(kTag, "Cannot launch %s: capability %s requires API >=%u (%s)",
             manifest.file_name, requirements.entries[failed].capability,
             static_cast<unsigned>(requirements.entries[failed].minApi), reason(result));
  else
    ESP_LOGE(kTag, "Cannot launch %s: invalid requirement set", manifest.file_name);
  return false;
}
