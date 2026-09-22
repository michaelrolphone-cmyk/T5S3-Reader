#pragma once
#include <RiscProviderV2.h>
#include <cstddef>
#include <cstdint>

/* Generic module loader; cannot include USB, UART, GPIO or board headers.
 * Its caller must authenticate a signed manifest, pin dependencies, resolve
 * API versions and authorize provider execution. */
namespace RuntimeProviders {
class ModuleV2 final {
 public:
  /* Failed also denotes quarantine after a partial start/teardown: code and
   * lower providers remain pinned but no new consumer may use its capability.
   * unload() retries verified quiescence; it never force-unmaps hardware. */
  enum class State : uint8_t { Absent, Active, Failed };
  ModuleV2() = default;
  ModuleV2(const ModuleV2&) = delete;
  ModuleV2& operator=(const ModuleV2&) = delete;
  /* Legacy/unprivileged loader, retained for host graph tests and modules not
   * requiring OS/CPU privilege. Never grants privileged imports. */
  bool load(const char* validatedElf, const char* expectedId,
            const char* expectedCapability, uint32_t expectedApi,
            const risc_provider_dependency_v1* dependencies, size_t count);
  /* PRIVATE firmware admission path. The caller MUST authenticate the signed
   * package/identity/ABI, exact signed entry digest AND canonical exact import
   * declarations. This function snapshots candidate bytes, hashes that
   * private snapshot and relocates from ONLY that matching snapshot.
   * Digest/import declarations supplied by an untrusted caller are NOT signer
   * authentication. Host builds deny this path; ownership still requires
   * separate grants and successful quiescence before unmapping. */
  bool loadVerifiedBytes(const uint8_t* candidateBytes, size_t length,
                         const uint8_t authenticatedSha256[32],
                         const char* const* signedImports, size_t signedImportCount,
                         const char* expectedId, const char* expectedCapability,
                         uint32_t expectedApi,
                         const risc_provider_dependency_v1* dependencies,
                         size_t count);
  bool pinConsumer();
  bool unpinConsumer();
  bool unload();
  const void* capability() const { return state_ == State::Active ? api_ : nullptr; }
  const char* lastError() const { return error_; }
  State state() const { return state_; }
  uint32_t consumers() const { return consumers_; }
 private:
  char error_[160]{};
  void report(const char* id, const char* stage, int code = 0);
  bool activateMapped(risc_driver_get_v2_fn get, const char* expectedId,
                      const char* expectedCapability, uint32_t expectedApi,
                      const risc_provider_dependency_v1* dependencies, size_t count);
  bool closeMapped();
  void* handle_ = nullptr;
  const risc_driver_v2* driver_ = nullptr;
  const void* api_ = nullptr;
  uint32_t consumers_ = 0;
  bool privileged_image_ = false;
  State state_ = State::Absent;
};
}  // namespace RuntimeProviders
