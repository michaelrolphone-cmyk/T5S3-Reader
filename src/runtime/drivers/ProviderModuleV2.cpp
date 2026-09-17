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
  if (candidate && candidate->abi_version == RISC_PROVIDER_DRIVER_ABI_V2 &&
      candidate->struct_size >= sizeof(risc_driver_v2) &&
      candidate->driver_id && candidate->capability_id &&
      std::strcmp(candidate->driver_id, expectedId) == 0 &&
      std::strcmp(candidate->capability_id, expectedCapability) == 0 &&
      candidate->capability_api == expectedApi && candidate->capability &&
      candidate->start && candidate->stop && candidate->start(deps, count)) {
    driver_ = candidate;
    api_ = candidate->capability;
    state_ = State::Active;
    return true;
  }
  /* A failing start must unwind partial acquisition. A malformed module
   * cannot be trusted with callbacks; never call stop unless start ran. */
  if (candidate && candidate->abi_version == RISC_PROVIDER_DRIVER_ABI_V2 &&
      candidate->struct_size >= sizeof(risc_driver_v2) && candidate->start &&
      candidate->stop && candidate->driver_id && candidate->capability_id &&
      std::strcmp(candidate->driver_id, expectedId) == 0 &&
      std::strcmp(candidate->capability_id, expectedCapability) == 0 &&
      candidate->capability_api == expectedApi && candidate->capability)
    candidate->stop();
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
  if (consumers_ || (handle_ && state_ == State::Failed)) return false;
  if (state_ == State::Active && driver_) driver_->stop();
  api_ = nullptr;
  driver_ = nullptr;
  if (handle_) {
    if (dlclose(handle_) != 0) {
      state_ = State::Failed;
      return false;
    }
    handle_ = nullptr;
  }
  state_ = State::Absent;
  return true;
}
}  // namespace RuntimeProviders
