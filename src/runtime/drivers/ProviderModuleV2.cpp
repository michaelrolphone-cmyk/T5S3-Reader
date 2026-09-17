#include "ProviderModuleV2.h"
#include <cstring>
#include <limits>
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
}  // namespace

bool ModuleV2::load(const char* path, const char* expectedId,
                    const char* expectedCapability, uint32_t expectedApi,
                    const risc_provider_dependency_v1* deps, size_t count) {
  if (handle_ || !path || !path[0] || !expectedId || !expectedId[0] ||
      !expectedCapability || !expectedCapability[0] || !expectedApi ||
      !validDependencies(deps, count)) return false;
  state_ = State::Failed;
  (void)dlerror();
  handle_ = dlopen(path, RTLD_NOW);
  if (!handle_) return false;
  (void)dlerror();
  auto get = reinterpret_cast<risc_driver_get_v2_fn>(dlsym(handle_, "t5_driver_get"));
  const char* error = dlerror();
  const risc_driver_v2* candidate = (!error && get) ? get(RISC_PROVIDER_DRIVER_ABI_V2) : nullptr;
  const bool valid = candidate && candidate->abi_version == RISC_PROVIDER_DRIVER_ABI_V2 &&
      candidate->struct_size >= RISC_DRIVER_V2_BASE_SIZE &&
      candidate->driver_id && candidate->capability_id &&
      std::strcmp(candidate->driver_id, expectedId) == 0 &&
      std::strcmp(candidate->capability_id, expectedCapability) == 0 &&
      candidate->capability_api == expectedApi && candidate->capability &&
      candidate->start && candidate->stop;
  if (valid) {
    if (candidate->start(deps, count)) {
      driver_ = candidate;
      api_ = candidate->capability;
      state_ = State::Active;
      return true;
    }
    /* A rejected start may have acquired physical resources. If the provider
     * cannot prove it has quiesced, keep its code mapped and retain pins on
     * lower providers. unload() is the explicit retry/recovery path. */
    if (hasQuiesce(candidate) && !candidate->quiesce()) {
      driver_ = candidate;
      return false;
    }
    candidate->stop();
  }
  if (dlclose(handle_) == 0) handle_ = nullptr;
  return false;
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
  /* A valid driver with an unsuccessful start OR teardown may still own
   * hardware. Retry quiesce in its mapped image before stop or dlclose. */
  if (state_ == State::Failed && handle_ && driver_) {
    if (!hasQuiesce(driver_) || !driver_->quiesce()) return false;
    driver_->stop();
    driver_ = nullptr;
  } else if (state_ == State::Active && driver_) {
    if (hasQuiesce(driver_) && !driver_->quiesce()) {
      /* A partially completed teardown is NOT an active usable provider.
       * Re-granting it could access freed DMA, handles, or power resources. */
      api_ = nullptr;
      state_ = State::Failed;
      return false;
    }
    driver_->stop();
    driver_ = nullptr;
  }
  api_ = nullptr;
  if (handle_) {
    if (dlclose(handle_) != 0) {
      /* The hardware is already stopped, so retry ONLY dlclose; do not call
       * into the stopped driver again if the linker later recovers. */
      state_ = State::Failed;
      return false;
    }
    handle_ = nullptr;
  }
  state_ = State::Absent;
  return true;
}
}  // namespace RuntimeProviders
