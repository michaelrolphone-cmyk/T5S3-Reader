#include "runtime/drivers/DeviceProviderExecutorV2.h"
#include <openssl/evp.h>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

using RuntimePackages::DeviceProviderExecutorV2;
using RuntimePackages::ManagerProviderCandidateV2;
using RuntimeProviders::GraphV2;
using RuntimeProviders::RequirementV2;
using RuntimeProviders::SpecV2;

int main() {
  uint8_t image[64]{};
  image[0] = 0x7f; image[1] = 'E'; image[2] = 'L'; image[3] = 'F';
  image[4] = 1; image[5] = 1; image[16] = 3; image[18] = 94;
  image[20] = 1;
  uint8_t digest[32]{};
  unsigned digestLength = 0;
  assert(EVP_Digest(image, sizeof(image), digest, &digestLength,
                    EVP_sha256(), nullptr) == 1 && digestLength == 32);
  char id[64] = "fixture-manager";
  char capability[64] = "cap.manager";
  char importOne[128] = "esp_intr_alloc";
  char importTwo[128] = "malloc";
  const char* imports[] = {importOne, importTwo};
  char dependencyName[64] = "cap.root";
  RequirementV2 dependencies[] = {{dependencyName, 1}};
  ManagerProviderCandidateV2 candidate{};
  candidate.driverId = id;
  candidate.provides = capability;
  candidate.providesApi = 1;
  candidate.requirements = dependencies;
  candidate.requirementCount = 1;
  candidate.elfBytes = image;
  candidate.elfLength = sizeof(image);
  candidate.importedSymbols = imports;
  candidate.importedSymbolCount = 2;
  candidate.declaredSha256 = digest;

  // A self-asserted checksum does not grant privileges via public admission.
  SpecV2 forged{id, nullptr, capability, 1, dependencies, 1};
  forged.requiredOsCpuAbi = 1;
  forged.verifiedElfBytes = image;
  forged.verifiedElfLength = sizeof(image);
  forged.signedImports = imports;
  forged.signedImportCount = 2;
  std::memcpy(forged.authenticatedElfSha256, digest, 32);
  GraphV2 graph;
  assert(!graph.addVerified(forged));
  assert(DeviceProviderExecutorV2::registerManagerValidated(graph, candidate));
  assert(graph.moduleCount() == 1);
  std::strcpy(id, "forged-driver");
  std::strcpy(capability, "cap.forged");
  std::strcpy(importOne, "forged_symbol");
  std::strcpy(importTwo, "other_symbol");
  std::strcpy(dependencyName, "cap.forged");
  image[0] = 0;
  std::memset(digest, 0, sizeof(digest));
  assert(!graph.acquire("cap.forged", 1).slot);
  assert(!graph.acquire("cap.manager", 1).slot);
  assert(graph.shutdown());

  image[0] = 0x7f;
  std::strcpy(id, "fixture-manager");
  std::strcpy(capability, "cap.manager");
  std::strcpy(importOne, "esp_intr_alloc");
  std::strcpy(importTwo, "malloc");
  std::strcpy(dependencyName, "cap.root");
  assert(EVP_Digest(image, sizeof(image), digest, &digestLength,
                    EVP_sha256(), nullptr) == 1 && digestLength == 32);
  GraphV2 malformed;
  digest[0] ^= 0xff;
  assert(!DeviceProviderExecutorV2::registerManagerValidated(malformed, candidate));
  digest[0] ^= 0xff;
  image[8] ^= 0x55;
  assert(!DeviceProviderExecutorV2::registerManagerValidated(malformed, candidate));
  image[8] ^= 0x55;
  auto invalid = candidate;
  invalid.requiredOsCpuAbi = 2;
  assert(!DeviceProviderExecutorV2::registerManagerValidated(malformed, invalid));
  invalid = candidate; invalid.elfBytes = nullptr;
  assert(!DeviceProviderExecutorV2::registerManagerValidated(malformed, invalid));
  invalid = candidate; invalid.elfLength = 51;
  assert(!DeviceProviderExecutorV2::registerManagerValidated(malformed, invalid));
  // A nonnull, exact empty declaration is valid structural metadata. The
  // private relocation matcher still rejects undeclared ELF undefined symbols.
  // Missing metadata remains invalid even when import count is zero.
  invalid = candidate; invalid.importedSymbols = nullptr; invalid.importedSymbolCount = 0;
  assert(!DeviceProviderExecutorV2::registerManagerValidated(malformed, invalid));
  invalid = candidate; invalid.importedSymbols = nullptr;
  assert(!DeviceProviderExecutorV2::registerManagerValidated(malformed, invalid));
  invalid = candidate; invalid.requirementCount = 1; invalid.requirements = nullptr;
  assert(!DeviceProviderExecutorV2::registerManagerValidated(malformed, invalid));
  invalid = candidate; invalid.providesApi = 0;
  assert(!DeviceProviderExecutorV2::registerManagerValidated(malformed, invalid));
  image[18] = 3;
  assert(!DeviceProviderExecutorV2::registerManagerValidated(malformed, candidate));
  image[18] = 94;
  image[16] = 2;
  assert(!DeviceProviderExecutorV2::registerManagerValidated(malformed, candidate));
  image[16] = 3;
  assert(malformed.moduleCount() == 0);
  invalid = candidate; invalid.importedSymbolCount = 0;
  GraphV2 explicitlyEmpty;
  assert(DeviceProviderExecutorV2::registerManagerValidated(explicitlyEmpty, invalid));
  assert(explicitlyEmpty.moduleCount() == 1 && explicitlyEmpty.shutdown());
  GraphV2 installed;
  image[8] ^= 0x55;
  assert(DeviceProviderExecutorV2::registerManagerValidated(installed, candidate, false));
  assert(installed.moduleCount() == 1 && installed.shutdown());
  image[8] ^= 0x55;
  GraphV2 badInstalled;
  image[0] = 0;
  assert(!DeviceProviderExecutorV2::registerManagerValidated(badInstalled, candidate, false));
  image[0] = 0x7f;
  candidate.declaredSha256 = nullptr;
  GraphV2 installedWithoutDigest;
  assert(DeviceProviderExecutorV2::registerManagerValidated(installedWithoutDigest, candidate, false));
  assert(installedWithoutDigest.moduleCount() == 1 && installedWithoutDigest.shutdown());
  assert(DeviceProviderExecutorV2::registerManagerValidated(malformed, candidate));
  assert(malformed.moduleCount() == 1 && malformed.shutdown());
  std::puts("Provider manager admission: unsigned ELF, integrity, explicit empty imports, forged privilege denial PASS");
}
