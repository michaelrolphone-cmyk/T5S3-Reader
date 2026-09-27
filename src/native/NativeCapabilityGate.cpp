#include "AppManifest.h"
#include "NativeSerialPortBridge.h"
#include "runtime/capabilities/AppDependencyBindings.h"
#include "runtime/drivers/GpsDriverRuntime.h"
#include "runtime/drivers/InstalledProviderGraph.h"
#include "runtime/packages/InstalledCapabilityResolver.h"
#include "runtime/resources/ExecutionContext.h"

#include <HalStorage.h>
#include <T5AppApi.h>
#include <esp_log.h>

#include <cstring>
#include <string>

namespace {
constexpr const char* kTag = "capability_gate";
RuntimeDevices::AppDependencyBindings bindings;

struct InstalledProviderBindings {
  RuntimeInstalledProviders::Lease leases[RuntimeDevices::kMaxAppRequirements]{};
  size_t count = 0;
  uint32_t owner = 0;
};
InstalledProviderBindings installedBindings;

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
  const std::string elf(sdPath + 3);
  if (elf.size() < 5 || elf.compare(elf.size() - 4, 4, ".elf") != 0) return false;
  const std::string sidecar = elf.substr(0, elf.size() - 4) + ".json";
  if (!Storage.exists(sidecar.c_str())) return true;
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
  for (size_t i = 0; i < requirements.count; ++i)
    if (std::strcmp(requirements.entries[i].capability, "location.position") == 0) {
      (void)GpsDriverRuntime::available();
      break;
    }
  nativeDeviceDiscoveryTick();
}

bool releaseInstalledBindings(uint32_t invocation) {
  if (!installedBindings.owner) return true;
  if (!invocation || installedBindings.owner != invocation) return false;

  bool okay = true;
  size_t retained = 0;
  for (size_t i = installedBindings.count; i > 0; --i) {
    const auto original = installedBindings.leases[i - 1];
    auto lease = original;
    if (!RuntimeInstalledProviders::release(&lease)) {
      installedBindings.leases[retained++] = original;
      okay = false;
    }
  }
  for (size_t i = retained; i < RuntimeDevices::kMaxAppRequirements; ++i)
    installedBindings.leases[i] = {};
  installedBindings.count = retained;
  if (!retained) installedBindings.owner = 0;
  return okay;
}

void releaseAllDependencies(uint32_t invocation) {
  bindings.release(RuntimeDevices::systemRegistry(), invocation);
  if (!releaseInstalledBindings(invocation))
    ESP_LOGE(kTag, "Mandatory installed provider did not quiesce during cleanup");
}

void cleanupDependencies(void*, uint32_t invocation) {
  releaseAllDependencies(invocation);
}
}  // namespace

// Firmware-only owner-task hooks; none are ELF imports.
extern "C" bool native_app_capabilities_ready(const char* sd_path) {
  RuntimeDevices::AppCapabilityRequirements requirements{};
  bool hasManifest = false;
  if (!loadRequirements(sd_path, requirements, hasManifest)) return false;
  if (!hasManifest || !requirements.count) return true;

  publishProviders(requirements);
  auto& registry = RuntimeDevices::systemRegistry();
  RuntimePackages::InstalledCapabilitySnapshot* installed = nullptr;

  for (size_t i = 0; i < requirements.count; ++i) {
    const auto& requested = requirements.entries[i];
    const auto live = RuntimeDevices::resolveRequirement(registry, requested);
    if (live == RuntimeDevices::RequirementResult::Ready) continue;

    if (!installed)
      installed = RuntimePackages::captureInstalledCapabilities();
    const uint32_t installedApi = installed
        ? RuntimePackages::versionInInstalledSnapshot(installed, requested.capability)
        : 0;
    const auto result = RuntimeDevices::resolveRequirementWithInstalledProvider(
        registry, requested, installedApi);
    if (result == RuntimeDevices::RequirementResult::Ready) continue;

    if (installed) RuntimePackages::releaseInstalledCapabilities(installed);
    ESP_LOGE(kTag, "Cannot launch %s: capability %s requires API >=%u (%s; installed=%lu)",
             sd_path, requested.capability, static_cast<unsigned>(requested.minApi),
             reason(result), static_cast<unsigned long>(installedApi));
    return false;
  }

  if (installed) RuntimePackages::releaseInstalledCapabilities(installed);
  return true;
}

extern "C" bool native_app_capabilities_bind(const char* sd_path) {
  RuntimeDevices::AppCapabilityRequirements requirements{};
  bool hasManifest = false;
  if (!loadRequirements(sd_path, requirements, hasManifest)) return false;
  if (!hasManifest || !requirements.count) return true;

  auto* context = RuntimeResources::ExecutionContext::current();
  if (!context || !context->running(context->id()) ||
      !t5_app_get_api(T5_APP_ABI_VERSION)) return false;
  if (bindings.owner() || installedBindings.owner || installedBindings.count) {
    ESP_LOGE(kTag, "Previous mandatory dependency binding is still active");
    return false;
  }

  publishProviders(requirements);
  auto& registry = RuntimeDevices::systemRegistry();
  RuntimeDevices::AppCapabilityRequirements registryRequirements{};
  size_t registryOriginalIndex[RuntimeDevices::kMaxAppRequirements]{};
  const uint32_t invocation = context->id();

  for (size_t i = 0; i < requirements.count; ++i) {
    const auto& requested = requirements.entries[i];
    if (RuntimeDevices::resolveRequirement(registry, requested) ==
        RuntimeDevices::RequirementResult::Ready) {
      registryOriginalIndex[registryRequirements.count] = i;
      registryRequirements.entries[registryRequirements.count++] = requested;
      continue;
    }

    if (!installedBindings.owner) installedBindings.owner = invocation;
    RuntimeInstalledProviders::Lease lease{};
    if (!RuntimeInstalledProviders::acquireCapability(
            requested.capability, requested.minApi, &lease) ||
        !lease.grant.slot || !lease.interface) {
      ESP_LOGE(kTag,
               "Cannot activate mandatory installed provider %s API >=%u for %s: %s",
               requested.capability, static_cast<unsigned>(requested.minApi), sd_path,
               RuntimeInstalledProviders::lastError());
      releaseInstalledBindings(invocation);
      return false;
    }
    installedBindings.leases[installedBindings.count++] = lease;
  }

  size_t subsetFailed = 0;
  if (!bindings.bind(registry, registryRequirements, invocation, &subsetFailed)) {
    const size_t failed = subsetFailed < registryRequirements.count
        ? registryOriginalIndex[subsetFailed] : requirements.count;
    ESP_LOGE(kTag, "Cannot reserve mandatory live dependency %lu for %s",
             static_cast<unsigned long>(failed), sd_path);
    releaseAllDependencies(invocation);
    return false;
  }

  if (!context->track(RuntimeResources::ExecutionContext::Resource::Dependencies,
                      cleanupDependencies)) {
    releaseAllDependencies(invocation);
    ESP_LOGE(kTag, "Execution context has no dependency cleanup slot");
    return false;
  }
  return true;
}

// Called by the trusted loader before dlclose and on every early-exit path.
// Context cleanup retains the same idempotent fallback for unexpected exit.
extern "C" void native_app_capabilities_release(void) {
  auto* context = RuntimeResources::ExecutionContext::current();
  if (!context || !context->id() || bindings.owner() != context->id() ||
      !t5_app_get_api(T5_APP_ABI_VERSION)) return;
  const uint32_t invocation = context->id();
  releaseAllDependencies(invocation);
  if (!installedBindings.owner)
    (void)context->untrack(RuntimeResources::ExecutionContext::Resource::Dependencies,
                           invocation);
}
