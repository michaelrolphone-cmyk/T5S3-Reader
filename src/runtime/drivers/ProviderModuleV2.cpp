#include "ProviderModuleV2.h"
#include <cstring>
#include <limits>
#ifdef ESP_PLATFORM
#include <cstdlib>
extern "C" {
#include <esp_elf.h>
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
}  // namespace

bool ModuleV2::closeMapped() {
  if (!handle_) return true;
#ifdef ESP_PLATFORM
  if (privileged_image_) {
    /* The caller must establish quiescence BEFORE reaching this branch.
     * This frees only generic ELF allocations, never hardware resources. */
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
  const risc_driver_v2* candidate = get ? get(RISC_PROVIDER_DRIVER_ABI_V2) : nullptr;
  const bool valid = candidate && candidate->abi_version == RISC_PROVIDER_DRIVER_ABI_V2 &&
      candidate->struct_size >= RISC_DRIVER_V2_BASE_SIZE &&
      candidate->driver_id && candidate->capability_id &&
      std::strcmp(candidate->driver_id, expectedId) == 0 &&
      std::strcmp(candidate->capability_id, expectedCapability) == 0 &&
      candidate->capability_api == expectedApi && candidate->capability &&
      candidate->start && candidate->stop &&
      (!privileged_image_ || hasQuiesce(candidate));
  if (!valid) return false;
  if (candidate->start(deps, count)) {
    driver_ = candidate;
    api_ = candidate->capability;
    state_ = State::Active;
    return true;
  }
  /* A rejected start may have acquired physical resources. Keep the code
   * and dependency providers pinned until its quiesce can prove shutdown. */
  if (hasQuiesce(candidate) && !candidate->quiesce()) {
    driver_ = candidate;
    return false;
  }
  candidate->stop();
  return false;
}

bool ModuleV2::load(const char* path, const char* expectedId,
                    const char* expectedCapability, uint32_t expectedApi,
                    const risc_provider_dependency_v1* deps, size_t count) {
  if (handle_ || !path || !path[0] ||
      !validRequest(expectedId, expectedCapability, expectedApi, deps, count))
    return false;
  state_ = State::Failed;
  (void)dlerror();
  handle_ = dlopen(path, RTLD_NOW);
  if (!handle_) return false;
  privileged_image_ = false;
  (void)dlerror();
  auto get = reinterpret_cast<risc_driver_get_v2_fn>(dlsym(handle_, "t5_driver_get"));
  const char* error = dlerror();
  if (!error && activateMapped(get, expectedId, expectedCapability,
                               expectedApi, deps, count)) return true;
  if (driver_) return false; /* failed quiesce; deliberately keep ELF mapped */
  (void)closeMapped();
  return false;
}

bool ModuleV2::loadVerifiedBytes(const uint8_t* verifiedBytes, size_t length,
                                 const char* expectedId,
                                 const char* expectedCapability,
                                 uint32_t expectedApi,
                                 const risc_provider_dependency_v1* deps,
                                 size_t count) {
#ifdef ESP_PLATFORM
  if (handle_ || !verifiedBytes || !length ||
      !validRequest(expectedId, expectedCapability, expectedApi, deps, count))
    return false;
  state_ = State::Failed;
  /* Trust/manifest/import verification MUST already have succeeded. Using the
   * verified buffer, rather than reopening a path, closes a verification TOCTOU.
   * No application-facing API exposes this private firmware entry point. */
  auto* image = static_cast<esp_elf_t*>(std::malloc(sizeof(esp_elf_t)));
  if (!image) return false;
  if (esp_elf_relocate_privileged_verified_v1(image, verifiedBytes, length) != 0) {
    std::free(image);
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
  if (activateMapped(get, expectedId, expectedCapability,
                     expectedApi, deps, count)) return true;
  if (driver_) return false; /* failed quiesce; never unmap active hardware */
  (void)closeMapped();
  return false;
#else
  /* Host tests exercise the legacy graph via POSIX dlopen; they must never
   * simulate success for an ESP32 privileged native ELF relocation. */
  (void)verifiedBytes; (void)length; (void)expectedId;
  (void)expectedCapability; (void)expectedApi; (void)deps; (void)count;
  return false;
#endif
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

bool ModuleV2::unload() {
  if (consumers_) return false;
  /* A failed start/teardown may still own IRQs, DMA or tasks. Retry quiesce
   * in the mapped image; do not release lower providers until it succeeds. */
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
  api_ = nullptr;
  if (!closeMapped()) {
    state_ = State::Failed;
    return false;
  }
  state_ = State::Absent;
  return true;
}
}  // namespace RuntimeProviders
