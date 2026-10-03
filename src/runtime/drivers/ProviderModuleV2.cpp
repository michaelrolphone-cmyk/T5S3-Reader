#include "ProviderModuleV2.h"
#include "../../../lib/hal/RuntimeFaultRetention.h"
#ifdef ESP_PLATFORM
#include "runtime/packages/PackageExecutableAdmission.h"
#endif
#include <cstring>
#include <cstdio>
#include <limits>
#ifdef ESP_PLATFORM
#include <cstdlib>
#include <Logging.h>
extern "C" {
#include <esp_elf.h>
#include <esp_heap_caps.h>
#include <mbedtls/sha256.h>
#include <private/esp_privileged_elf.h>
}
#endif
extern "C" {
#include <esp_dlfcn.h>
}

namespace RuntimeProviders {
namespace {
bool validDependencies(const risc_provider_dependency_v1* deps, size_t count) {
  if (count > 16 || (count && !deps)) return false;
  for (size_t i = 0; i < count; ++i) {
    if (!deps[i].capability_id || !deps[i].capability_id[0] ||
        !deps[i].api_version || !deps[i].api) return false;
    for (size_t j = 0; j < i; ++j)
      if (std::strcmp(deps[i].capability_id, deps[j].capability_id) == 0)
        return false;
  }
  return true;
}
bool hasQuiesce(const risc_driver_v2* driver) {
  return driver && driver->struct_size >= sizeof(risc_driver_v2) && driver->quiesce;
}
bool validRequest(const char* expectedId, const char* expectedCapability,
                  uint32_t expectedApi,
                  const risc_provider_dependency_v1* deps, size_t count) {
  return expectedId && expectedId[0] && expectedCapability &&
         expectedCapability[0] && expectedApi && validDependencies(deps, count);
}
void trace(const char* id, const char* stage) {
  (void)id; (void)stage;
#ifdef ESP_PLATFORM
  LOG_INF("PROV", "PROVREF id=%s stage=%s", id ? id : "?", stage);
#endif
}
} // namespace

void ModuleV2::report(const char* id, const char* stage, int code) {
  // Keep the original cause even if teardown subsequently fails.
  if (!error_[0]) std::snprintf(error_, sizeof(error_), "%s: %s rc=%d (0x%x)",
                              id ? id : "?", stage, code, static_cast<unsigned>(code));
  // Some ESP_PLATFORM host harnesses stub LOG_ERR to a no-op. Keep parameters
  // explicitly used in both logging-enabled and logging-disabled builds.
  (void)id; (void)stage; (void)code;
#ifdef ESP_PLATFORM
  LOG_ERR("PROV", "PROVREF id=%s failure=%s code=%d", id ? id : "?", stage, code);
#endif
}

bool ModuleV2::copyProviderError(char* destination, size_t capacity) const {
  if (!destination || !capacity) return false;
  destination[0] = 0;
  if (driver_ && driver_->struct_size >= sizeof(risc_driver_diagnostics_v2)) {
    const auto* diagnostics = reinterpret_cast<const risc_driver_diagnostics_v2*>(driver_);
    if (diagnostics->last_error && diagnostics->last_error(destination, capacity) && destination[0])
      return true;
  }
  if (!error_[0]) return false;
  std::snprintf(destination, capacity, "%s", error_);
  return destination[0] != 0;
}

bool ModuleV2::closeMapped() {
  risc_runtime_retention_guard();
  if (!handle_) return true;
#ifdef ESP_PLATFORM
  if (privileged_image_) {
    // Only generic memory is released here. Caller established quiescence.
    esp_elf_deinit(static_cast<esp_elf_t*>(handle_));
    std::free(handle_);
    handle_ = nullptr;
    privileged_image_ = false;
    return true;
  }
#endif
  if (dlclose(handle_) != 0) return false;
  handle_ = nullptr;
  privileged_image_ = false;
  return true;
}

bool ModuleV2::activateMapped(risc_driver_get_v2_fn get, const char* expectedId,
                              const char* expectedCapability, uint32_t expectedApi,
                              const risc_provider_dependency_v1* deps, size_t count) {
  if (risc_runtime_retention_required()) return false;
  const risc_driver_v2* candidate = get ? get(RISC_PROVIDER_DRIVER_ABI_V2) : nullptr;
  const bool valid = candidate && candidate->abi_version == RISC_PROVIDER_DRIVER_ABI_V2 &&
      candidate->struct_size >= RISC_DRIVER_V2_BASE_SIZE &&
      candidate->driver_id && candidate->capability_id &&
      std::strcmp(candidate->driver_id, expectedId) == 0 &&
      std::strcmp(candidate->capability_id, expectedCapability) == 0 &&
      candidate->capability_api == expectedApi && candidate->capability &&
      candidate->start && candidate->stop &&
      (!privileged_image_ || hasQuiesce(candidate));
  if (!valid) {
    report(expectedId, "elf-interface-or-identity");
    return false;
  }
  bool bound = true;
  if (candidate->struct_size >= sizeof(risc_driver_streams_v2)) {
    const auto* extended = reinterpret_cast<const risc_driver_streams_v2*>(candidate);
    if (extended->bind_streams) {
      if (!hasQuiesce(candidate) || !streamHost_ || !streamHost_->open ||
          !streamHost_->revoke || !streamHost_->close ||
          !(resourceIdentity_.id[0]
              ? streamHost_->openResources && streamHost_->openResources(&streamApi_, resourceIdentity_)
              : streamHost_->open(&streamApi_.streams))) {
        report(expectedId, "stream-context-unavailable");
        return false;
      }
      streamsRevoked_ = false;
      bound = extended->bind_streams(&streamApi_.streams);
    }
  }
  trace(expectedId, "hardware-start-begin");
  if (bound && candidate->start(deps, count)) {
    driver_ = candidate;
    api_ = candidate->capability;
    state_ = State::Active;
    trace(expectedId, "hardware-started");
    return true;
  }
  if (candidate->struct_size >= sizeof(risc_driver_diagnostics_v2)) {
    const auto* diagnostics = reinterpret_cast<const risc_driver_diagnostics_v2*>(candidate);
    char detail[112]{};
    if (diagnostics->last_error && diagnostics->last_error(detail, sizeof(detail))) {
      detail[sizeof(detail) - 1] = 0;
      if (detail[0]) std::snprintf(error_, sizeof(error_), "%s: %s", expectedId, detail);
    }
  }
  if (!error_[0]) report(expectedId, "start rejected; update driver for diagnostics");
  revokeStreams();
  // A rejected start may still own DMA, tasks, IRQs or a lower provider.
  if (hasQuiesce(candidate) && !candidate->quiesce()) {
    driver_ = candidate;
    report(expectedId, "hardware-quiesce-rejected");
    return false;
  }
  candidate->stop();
  closeStreams();
  return false;
}

bool ModuleV2::load(const char* path, const char* expectedId,
                    const char* expectedCapability, uint32_t expectedApi,
                    const risc_provider_dependency_v1* deps, size_t count) {
  if (!handle_) error_[0] = 0;
  if (handle_ || !path || !path[0] ||
      !validRequest(expectedId, expectedCapability, expectedApi, deps, count)) {
    report(expectedId, "invalid-elf-request");
    return false;
  }
  state_ = State::Failed;
  (void)dlerror();
  handle_ = dlopen(path, RTLD_NOW);
  if (!handle_) { report(expectedId, "elf-open-failed"); return false; }
  privileged_image_ = false;
  (void)dlerror();
  auto get = reinterpret_cast<risc_driver_get_v2_fn>(dlsym(handle_, "t5_driver_get"));
  const char* error = dlerror();
  if (error || !get) report(expectedId, "elf-entry-symbol-missing");
  if (!error && activateMapped(get, expectedId, expectedCapability,
                               expectedApi, deps, count)) return true;
  if (driver_) return false;
  (void)closeMapped();
  return false;
}

bool ModuleV2::loadVerifiedBytes(const uint8_t* candidateBytes, size_t length,
                                 const uint8_t contentSha256[32],
                                 const char* const* declaredImports,
                                 size_t declaredImportCount,
                                 const char* expectedId,
                                 const char* expectedCapability,
                                 uint32_t expectedApi,
                                 const risc_provider_dependency_v1* deps,
                                 size_t count) {
  if (!handle_) error_[0] = 0;
#ifdef ESP_PLATFORM
  // Nonnull import metadata and an exact zero count is valid for a truly
  // self-contained ELF. The private matcher checks both symbol tables.
  if (handle_ || !candidateBytes || !contentSha256 || !length ||
      !declaredImports || declaredImportCount > 128 ||
      length > 8u * 1024u * 1024u ||
      !validRequest(expectedId, expectedCapability, expectedApi, deps, count)) {
    report(expectedId, "invalid-elf-request");
    return false;
  }
  // The sole transitional firmware peripheral entry point is available to
  // the exact I2C provider only. The private relocator later matches these
  // declarations to BOTH ELF symbol tables, so a caller cannot hide imports.
  // Upstream USB/board-power providers must bind the i2c.bus capability,
  // never import the firmware transport themselves. This runs before mapping.
  bool importsFirmwareI2c = false;
  bool importsFirmwareDisplay = false;
  unsigned importsFirmwareSpi = 0;
  for (size_t i = 0; i < declaredImportCount; ++i) {
    if (!declaredImports[i]) {
      report(expectedId, "invalid-provider-import");
      return false;
    }
    if (std::strcmp(declaredImports[i], "risc_fw_i2c_transact_v1") == 0)
      importsFirmwareI2c = true;
    if (std::strcmp(declaredImports[i], "t5_video_get_api") == 0)
      importsFirmwareDisplay = true;
    if (std::strcmp(declaredImports[i], "risc_fw_spi_begin_v1") == 0 ||
        std::strcmp(declaredImports[i], "risc_fw_spi_select_v1") == 0 ||
        std::strcmp(declaredImports[i], "risc_fw_spi_transfer_v1") == 0 ||
        std::strcmp(declaredImports[i], "risc_fw_spi_end_v1") == 0)
      ++importsFirmwareSpi;
  }
  const bool isFirmwareI2cAdapter =
      std::strcmp(expectedId, "i2c-esp32s3-v2") == 0 &&
      std::strcmp(expectedCapability, "i2c.bus") == 0 && expectedApi == 1;
  if (importsFirmwareI2c != isFirmwareI2cAdapter) {
    report(expectedId, "i2c-firmware-compat-import-policy");
    return false;
  }
  const bool isSpiAdapter = std::strcmp(expectedId, "spi-esp32s3-v1") == 0 &&
      std::strcmp(expectedCapability, "spi.bus") == 0 && expectedApi == 1;
  if (isSpiAdapter ? importsFirmwareSpi != 4 : importsFirmwareSpi != 0) {
    report(expectedId, "spi-firmware-compat-import-policy");
    return false;
  }
  const bool isDisplayAdapter =
      std::strcmp(expectedId, "display-epd-video") == 0 &&
      std::strcmp(expectedCapability, "display.output") == 0 && expectedApi == 1;
  if (importsFirmwareDisplay != isDisplayAdapter) {
    report(expectedId, "display-firmware-compat-import-policy");
    return false;
  }
  state_ = State::Failed;
  trace(expectedId, "elf-relocate-begin");
  auto* snapshot = static_cast<uint8_t*>(
      heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!snapshot) snapshot = static_cast<uint8_t*>(
      heap_caps_malloc(length, MALLOC_CAP_8BIT));
  if (!snapshot) {
    report(expectedId, "elf-snapshot-oom", static_cast<int>(length));
    return false;
  }
  std::memcpy(snapshot, candidateBytes, length);
  // Verify the exact owned bytes that relocation consumes. Reusable proof is
  // bound to the epoch captured BEFORE the manager read these bytes; never
  // attach a fresh stamp to an older snapshot after intervening writes.
  RuntimePackages::VerifiedImageCopy copyEvidence;
  const bool graphOwned = ownedImage_ && ownedImage_ == candidateBytes && ownedImageBytes_ == length;
  const bool verifiedCopy = graphOwned && ownedImageVerified_ &&
      !std::memcmp(ownedImageDigest_, contentSha256, sizeof(ownedImageDigest_));
  if (verifiedCopy) {
    copyEvidence.bytes_ = snapshot; copyEvidence.size_ = length;
    std::memcpy(copyEvidence.digest_, ownedImageDigest_, sizeof(copyEvidence.digest_));
  }
  bool admitted = false;
  if (resourceIdentity_.id[0]) {
    admitted = RuntimePackages::admitInstalledExecutableSnapshot(resourceIdentity_,
        packageManifestSha256_, contentSha256, snapshot, length, packageSourceStamp_,
        verifiedCopy ? &copyEvidence : nullptr);
  } else {
    uint8_t digest[32]{};
    admitted = verifiedCopy || (RuntimePackages::packageSnapshotDigest(snapshot,length,digest) &&
        !std::memcmp(digest,contentSha256,sizeof(digest)));
  }
  if (!admitted) {
    heap_caps_free(snapshot);
    report(expectedId, "installed-snapshot-integrity");
    return false;
  }

  if (graphOwned) {
    std::memcpy(ownedImageDigest_, contentSha256, sizeof(ownedImageDigest_));
    ownedImageVerified_ = true;
  }

  auto* image = static_cast<esp_elf_t*>(std::malloc(sizeof(esp_elf_t)));
  if (!image) {
    heap_caps_free(snapshot);
    report(expectedId, "elf-handle-oom");
    return false;
  }
  const int result = esp_elf_relocate_privileged_verified_v1(
      image, snapshot, length, declaredImports, declaredImportCount);
  heap_caps_free(snapshot);
  if (result != 0) {
    std::free(image);
    report(expectedId, "elf-relocation-failed", result);
    return false;
  }
  handle_ = image;
  privileged_image_ = true;
  risc_driver_get_v2_fn get = nullptr;
  for (uint16_t i = 0; i < image->num; ++i) {
    if (image->symtab[i].name &&
        std::strcmp(image->symtab[i].name, "t5_driver_get") == 0) {
      get = reinterpret_cast<risc_driver_get_v2_fn>(image->symtab[i].addr);
      break;
    }
  }
  if (!get) report(expectedId, "elf-entry-symbol-missing");
  else trace(expectedId, "elf-relocated");
  if (activateMapped(get, expectedId, expectedCapability,
                     expectedApi, deps, count)) return true;
  if (driver_) return false;
  (void)closeMapped();
  return false;
#else
  (void)candidateBytes; (void)length; (void)contentSha256;
  (void)declaredImports; (void)declaredImportCount;
  (void)expectedId; (void)expectedCapability; (void)expectedApi;
  (void)deps; (void)count;
  return false;
#endif
}

bool ModuleV2::poll(uint32_t budgetMs) {
  if (!budgetMs || state_ != State::Active || !driver_ || !consumers_ ||
      driver_->struct_size < sizeof(risc_driver_poll_v2)) return false;
  const auto* extended = reinterpret_cast<const risc_driver_poll_v2*>(driver_);
  if (!extended->poll) return false;
  extended->poll(budgetMs);
  return true;
}
bool ModuleV2::pinConsumer() {
  if (state_ != State::Active || consumers_ == std::numeric_limits<uint32_t>::max())
    return false;
  ++consumers_;
  return true;
}

bool ModuleV2::unpinConsumer() {
  if (!consumers_) return false;
  --consumers_;
  return true;
}

void ModuleV2::revokeStreams() {
  if (streamApi_.streams.context && !streamsRevoked_) {
    streamHost_->revoke(streamApi_.streams.context);
    streamsRevoked_ = true;
  }
}
void ModuleV2::closeStreams() {
  if (!streamApi_.streams.context) return;
  revokeStreams();
  streamHost_->close(streamApi_.streams.context);
  streamApi_ = {};
}
bool ModuleV2::unload() {
  risc_runtime_retention_guard();
  if (consumers_) return false;
  revokeStreams();
  if (state_ == State::Failed && handle_ && driver_) {
    if (!hasQuiesce(driver_) || !driver_->quiesce()) return false;
    driver_->stop();
    driver_ = nullptr;
  } else if (state_ == State::Active && driver_) {
    if (hasQuiesce(driver_) && !driver_->quiesce()) {
      api_ = nullptr;
      state_ = State::Failed;
      return false;
    }
    driver_->stop();
    driver_ = nullptr;
  }
  closeStreams();
  api_ = nullptr;
  if (!closeMapped()) {
    state_ = State::Failed;
    return false;
  }
  state_ = State::Absent;
  return true;
}
} // namespace RuntimeProviders
