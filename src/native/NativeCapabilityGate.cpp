#include "AppManifest.h"
#include "NativeSerialPortBridge.h"
#include "runtime/capabilities/AppDependencyBindings.h"
#include "runtime/drivers/GpsDriverRuntime.h"
#include "runtime/resources/ExecutionContext.h"

#include <HalStorage.h>
#include <T5AppApi.h>
#include <esp_log.h>

#include <cstring>
#include <string>

namespace {
constexpr const char* kTag = "capability_gate";
RuntimeDevices::AppDependencyBindings bindings;

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

bool loadRequirements(const char* sdPath, RuntimeDevices::AppCapabilityRequirements& requirements,
                      bool& hasManifest) {
  hasManifest = false;
  if (!sdPath || std::strncmp(sdPath, "/sd/", 4) != 0) return false;
  const std::string elf(sdPath + 3);  // VFS /sd/Foo -> HalStorage /Foo.
  if (elf.size() < 5 || elf.compare(elf.size() - 4, 4, ".elf") != 0) return false;
  const std::string sidecar = elf.substr(0, elf.size() - 4) + ".json";
  if (!Storage.exists(sidecar.c_str())) return true;  // Legacy loose ELF.
  hasManifest = true;
  t5_app_manifest_t manifest{};
  if (!readAppManifest(sidecar.c_str(), manifest, nullptr, false, &requirements) ||
      !manifest.compatible ||
      elf.substr(elf.find_last_of('/') + 1) != manifest.file_name) {
    ESP_LOGE(kTag, "Invalid, incompatible or mismatched application sidecar: %s", sidecar.c_str());
    return false;
  }
  return true;
}

void publishProviders(const RuntimeDevices::AppCapabilityRequirements& requirements) {
  // GNSS probes availability without starting UART or mapping the provider ELF.
  for (size_t i = 0; i < requirements.count; ++i)
    if (std::strcmp(requirements.entries[i].capability, "location.position") == 0) {
      (void)GpsDriverRuntime::available();
      break;
    }
  nativeDeviceDiscoveryTick();
}

void cleanupDependencies(void*, uint32_t invocation) {
  bindings.release(RuntimeDevices::systemRegistry(), invocation);
}
}  // namespace

// Firmware-only owner-task hooks; neither function is an ELF import.
extern "C" bool native_app_capabilities_ready(const char* sd_path) {
  RuntimeDevices::AppCapabilityRequirements requirements{};
  bool hasManifest = false;
  if (!loadRequirements(sd_path, requirements, hasManifest)) return false;
  if (!hasManifest || !requirements.count) return true;
  publishProviders(requirements);
  size_t failed = 0;
  const auto result = RuntimeDevices::resolveRequirements(
      RuntimeDevices::systemRegistry(), requirements, &failed);
  if (result == RuntimeDevices::RequirementResult::Ready) return true;
  if (failed < requirements.count)
    ESP_LOGE(kTag, "Cannot launch %s: capability %s requires API >=%u (%s)",
             sd_path, requirements.entries[failed].capability,
             static_cast<unsigned>(requirements.entries[failed].minApi), reason(result));
  return false;
}

extern "C" bool native_app_capabilities_bind(const char* sd_path) {
  RuntimeDevices::AppCapabilityRequirements requirements{};
  bool hasManifest = false;
  if (!loadRequirements(sd_path, requirements, hasManifest)) return false;
  if (!hasManifest || !requirements.count) return true;
  auto* context = RuntimeResources::ExecutionContext::current();
  if (!context || !context->running(context->id()) ||
      !t5_app_get_api(T5_APP_ABI_VERSION)) return false;
  publishProviders(requirements);
  size_t failed = 0;
  auto& registry = RuntimeDevices::systemRegistry();
  if (!bindings.bind(registry, requirements, context->id(), &failed)) {
    ESP_LOGE(kTag, "Cannot reserve mandatory dependency %lu for %s",
             static_cast<unsigned long>(failed), sd_path);
    return false;
  }
  if (!context->track(RuntimeResources::ExecutionContext::Resource::Dependencies,
                      cleanupDependencies)) {
    bindings.release(registry, context->id());
    ESP_LOGE(kTag, "Execution context has no dependency cleanup slot");
    return false;
  }
  return true;
}
