#pragma once
#include <RiscProviderV2.h>
#include <cstddef>
#include <cstdint>

/* Generic module loader; cannot include USB, UART, GPIO or board headers.
 * Its caller must authenticate a signed manifest, pin dependencies, resolve
 * API versions and authorize provider execution. */
namespace RuntimeProviders {
struct StreamHostV1 {
  bool (*open)(risc_stream_provider_v1*);
  void (*revoke)(uint64_t);
  void (*close)(uint64_t);
  bool (*grant)(uint64_t, uint64_t, uint32_t, uint32_t, uint32_t);
  void (*revokeGrant)(uint64_t, uint64_t);
};
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
  bool setStreamHost(const StreamHostV1* host) {
    if (state_ != State::Absent || handle_) return false;
    streamHost_ = host; return true;
  }
  uint64_t streamContext() const { return state_ == State::Active ? streamApi_.context : 0; }
  bool poll(uint32_t budgetMs);
  bool pinConsumer();
  bool unpinConsumer();
  bool unload();
  const void* capability() const { return state_ == State::Active ? api_ : nullptr; }
  State state() const { return state_; }
  uint32_t consumers() const { return consumers_; }
 private:
  bool activateMapped(risc_driver_get_v2_fn get, const char* expectedId,
                      const char* expectedCapability, uint32_t expectedApi,
                      const risc_provider_dependency_v1* dependencies, size_t count);
  bool closeMapped();
  void revokeStreams();
  void closeStreams();
  const StreamHostV1* streamHost_ = nullptr;
  risc_stream_provider_v1 streamApi_{};
  bool streamsRevoked_ = false;
  void* handle_ = nullptr;
  const risc_driver_v2* driver_ = nullptr;
  const void* api_ = nullptr;
  uint32_t consumers_ = 0;
  bool privileged_image_ = false;
  State state_ = State::Absent;
};
}  // namespace RuntimeProviders
