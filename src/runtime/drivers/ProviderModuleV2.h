#pragma once
#include <RiscProviderV2.h>
#include <cstddef>
#include <cstdint>

/* Generic module loader; cannot include USB, UART, GPIO or board headers.
 * Its caller must verify a signed/integrity-checked manifest, pin dependency
 * provider ELFs, resolve API versions and authorize physical consumers. */
namespace RuntimeProviders {
class ModuleV2 final {
 public:
  enum class State : uint8_t { Absent, Active, Failed };
  ModuleV2() = default;
  ModuleV2(const ModuleV2&) = delete;
  ModuleV2& operator=(const ModuleV2&) = delete;
  bool load(const char* validatedElf, const char* expectedId,
            const char* expectedCapability, uint32_t expectedApi,
            const risc_provider_dependency_v1* dependencies, size_t count);
  bool pinConsumer();
  bool unpinConsumer();
  bool unload();
  const void* capability() const { return state_ == State::Active ? api_ : nullptr; }
  State state() const { return state_; }
  uint32_t consumers() const { return consumers_; }
 private:
  void* handle_ = nullptr;
  const risc_driver_v2* driver_ = nullptr;
  const void* api_ = nullptr;
  uint32_t consumers_ = 0;
  State state_ = State::Absent;
};
}  // namespace RuntimeProviders
