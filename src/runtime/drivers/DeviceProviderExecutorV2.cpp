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
bool xtensaDynamicallyLinkedElf(const uint8_t* bytes, size_t length) {
  return bytes && length >= 52 && length <= 8u * 1024u * 1024u &&
      bytes[0] == 0x7f && bytes[1] == 'E' && bytes[2] == 'L' &&
      bytes[3] == 'F' && bytes[4] == 1 && bytes[5] == 1 &&
      bytes[16] == 3 && bytes[17] == 0 &&
      bytes[18] == 94 && bytes[19] == 0 &&
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
bool equalDigest(const uint8_t* a, const uint8_t* b) {
  uint8_t difference = 0;
  for (size_t i = 0; i < 32; ++i)
    difference |= static_cast<uint8_t>(a[i] ^ b[i]);
  return difference == 0;
}
} // namespace

bool DeviceProviderExecutorV2::registerManagerValidated(
    RuntimeProviders::GraphV2& graph, const ManagerProviderCandidateV2& input) {
  // A nonnull declaration with count zero denotes an intentionally empty
  // exact import set. The private ELF matcher must find no undefined symbols.
  if (input.requiredOsCpuAbi != 1 || !input.driverId || !input.provides ||
      !input.providesApi || !input.importedSymbols ||
      input.importedSymbolCount > 128 ||
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
  const bool accepted = graph.addAuthenticatedPrivileged(spec);
  std::memset(calculated, 0, sizeof(calculated));
  return accepted;
}

} // namespace RuntimePackages
