#include "DeviceProviderExecutorV2.h"

#include <cstring>
#ifdef ESP_PLATFORM
extern "C" {
#include <mbedtls/sha256.h>
}
#else
#include <openssl/evp.h>
#endif

namespace RuntimePackages {
namespace {
// Structural preflight is independent of optional package signing. Complete
// section/symbol/relocation and privileged-import validation happens again on
// the loader's exact private relocation image, before executable mapping.
bool xtensaDynamicallyLinkedElf(const uint8_t* bytes, size_t length) {
  return bytes && length >= 52 && length <= 8u * 1024u * 1024u &&
      bytes[0] == 0x7f && bytes[1] == 'E' && bytes[2] == 'L' &&
      bytes[3] == 'F' && bytes[4] == 1 && bytes[5] == 1 &&
      bytes[16] == 3 && bytes[17] == 0 && // ELF32 ET_DYN
      bytes[18] == 94 && bytes[19] == 0 && // EM_XTENSA
      bytes[20] == 1 && bytes[21] == 0 && bytes[22] == 0 && bytes[23] == 0;
}
bool sha256(const uint8_t* bytes, size_t length, uint8_t digest[32]) {
#ifdef ESP_PLATFORM
  return mbedtls_sha256_ret(bytes, length, digest, 0) == 0;
#else
  unsigned int resultLength = 0;
  return EVP_Digest(bytes, length, digest, &resultLength,
                    EVP_sha256(), nullptr) == 1 && resultLength == 32;
#endif
}
// Avoid short-circuit comparison of package-provided digest bytes.
bool equalDigest(const uint8_t* a, const uint8_t* b) {
  uint8_t difference = 0;
  for (size_t i = 0; i < 32; ++i)
    difference |= static_cast<uint8_t>(a[i] ^ b[i]);
  return difference == 0;
}
} // namespace

bool DeviceProviderExecutorV2::registerManagerValidated(
    RuntimeProviders::GraphV2& graph, const ManagerProviderCandidateV2& input) {
  if (input.requiredOsCpuAbi != 1 || !input.driverId || !input.provides ||
      !input.providesApi || !input.importedSymbols ||
      !input.importedSymbolCount || input.importedSymbolCount > 128 ||
      input.requirementCount > RuntimeProviders::GraphV2::kMaxModules ||
      (input.requirementCount && !input.requirements) ||
      !xtensaDynamicallyLinkedElf(input.elfBytes, input.elfLength)) return false;
  uint8_t calculated[32]{};
  if (!sha256(input.elfBytes, input.elfLength, calculated)) return false;
  if (input.declaredSha256 &&
      !equalDigest(calculated, input.declaredSha256)) return false;

  RuntimeProviders::SpecV2 spec{input.driverId, nullptr, input.provides,
      input.providesApi, input.requirements, input.requirementCount};
  spec.requiredOsCpuAbi = 1;
  spec.verifiedElfBytes = input.elfBytes;
  spec.verifiedElfLength = input.elfLength;
  spec.signedImports = input.importedSymbols; // Legacy field name; unsigned is valid.
  spec.signedImportCount = input.importedSymbolCount;
  std::memcpy(spec.authenticatedElfSha256, calculated, sizeof(calculated));
  // Graph takes a PRIVATE copy of image, names and digest before returning.
  // Later ModuleV2 checks a SECOND copy's checksum and exact ABI imports.
  // Neither operation implicitly starts hardware or grants a consumer lease.
  const bool accepted = graph.addAuthenticatedPrivileged(spec);
  std::memset(calculated, 0, sizeof(calculated));
  return accepted;
}

} // namespace RuntimePackages
