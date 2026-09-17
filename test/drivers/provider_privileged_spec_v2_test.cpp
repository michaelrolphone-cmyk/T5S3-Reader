#include "runtime/drivers/ProviderGraphV2.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <type_traits>

using RuntimeProviders::GraphV2;
using RuntimeProviders::SpecV2;

static_assert(std::is_array<decltype(SpecV2::authenticatedElfSha256)>::value,
              "The authenticated image digest must be stored by value");

int main() {
  const uint8_t candidate[] = {0x7f, 'E', 'L', 'F', 1, 1};
  const char* const exact[] = {"esp_intr_alloc", "malloc"};
  const char* const duplicate[] = {"malloc", "malloc"};
  const char* const unsorted[] = {"malloc", "esp_intr_alloc"};
  SpecV2 privileged{"fixture-privileged", nullptr, "cap.generic", 1, nullptr, 0};
  privileged.requiredOsCpuAbi = 1;
  privileged.verifiedElfBytes = candidate;
  privileged.verifiedElfLength = sizeof(candidate);

  GraphV2 invalid;
  assert(!invalid.addVerified(privileged)); /* No signed-entry digest/imports. */
  assert(invalid.moduleCount() == 0);
  privileged.authenticatedElfSha256[0] = 0xa5;
  assert(!invalid.addVerified(privileged)); /* A digest is not a declaration. */
  privileged.signedImports = exact;
  privileged.signedImportCount = 2;
  assert(invalid.addVerified(privileged)); /* Private byte path needs no VFS name. */
  assert(invalid.moduleCount() == 1);
  privileged.authenticatedElfSha256[0] = 0;
  assert(!invalid.acquire("cap.generic", 1).slot); /* Host must not execute Xtensa. */
  assert(invalid.shutdown());

  GraphV2 malformed;
  privileged.authenticatedElfSha256[0] = 0xa5;
  privileged.requiredOsCpuAbi = 2;
  assert(!malformed.addVerified(privileged));
  privileged.requiredOsCpuAbi = 1;
  privileged.signedImports = duplicate;
  assert(!malformed.addVerified(privileged));
  privileged.signedImports = unsorted;
  assert(!malformed.addVerified(privileged));
  privileged.signedImports = nullptr;
  assert(!malformed.addVerified(privileged));
  privileged.signedImports = exact;
  privileged.signedImportCount = 129;
  assert(!malformed.addVerified(privileged));
  privileged.signedImportCount = 2;
  privileged.verifiedElfBytes = nullptr;
  assert(!malformed.addVerified(privileged));
  privileged.verifiedElfBytes = candidate;
  privileged.verifiedElfLength = 8u * 1024u * 1024u + 1u;
  assert(!malformed.addVerified(privileged));
  privileged.verifiedElfLength = sizeof(candidate);
  privileged.verifiedElfPath = "relative/driver.elf";
  assert(!malformed.addVerified(privileged));

  SpecV2 ordinary{"fixture-ordinary", "/sd/Drivers/ordinary/driver.elf",
                  "cap.other", 1, nullptr, 0};
  ordinary.authenticatedElfSha256[0] = 0xa5;
  assert(!malformed.addVerified(ordinary)); /* No privileged metadata on ordinary. */
  ordinary.authenticatedElfSha256[0] = 0;
  ordinary.signedImports = exact;
  ordinary.signedImportCount = 2;
  assert(!malformed.addVerified(ordinary));
  ordinary.signedImports = nullptr;
  ordinary.signedImportCount = 0;
  ordinary.verifiedElfPath = nullptr;
  assert(!malformed.addVerified(ordinary));
  ordinary.verifiedElfPath = "/sd/Drivers/ordinary/driver.elf";
  assert(malformed.addVerified(ordinary));
  assert(malformed.shutdown());
  std::puts("Provider spec: digest + exact import declarations required; host privilege denied PASS");
}
