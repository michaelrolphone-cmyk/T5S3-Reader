#include "runtime/drivers/ProviderGraphV2.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <type_traits>

using RuntimeProviders::GraphV2;
using RuntimeProviders::SpecV2;

// Friend test fixture models ONLY the compiler-visible firmware manager
// boundary. It does not replace real P-256 verification; PR #76 supplies that
// prerequisite before any production manager calls the private entry.
namespace RuntimePackages {
class DeviceProviderExecutorV2 {
 public:
  static bool admitForFixture(GraphV2& graph, const SpecV2& spec) {
    return graph.addAuthenticatedPrivileged(spec);
  }
};
}

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
  assert(!invalid.addVerified(privileged));
  assert(!RuntimePackages::DeviceProviderExecutorV2::admitForFixture(invalid, privileged));
  assert(invalid.moduleCount() == 0);
  privileged.authenticatedElfSha256[0] = 0xa5;
  assert(!invalid.addVerified(privileged));
  assert(!RuntimePackages::DeviceProviderExecutorV2::admitForFixture(invalid, privileged));
  privileged.signedImports = exact;
  privileged.signedImportCount = 2;
  assert(!invalid.addVerified(privileged)); // Even plausible hash+imports are NOT signing.
  assert(RuntimePackages::DeviceProviderExecutorV2::admitForFixture(invalid, privileged));
  assert(invalid.moduleCount() == 1);
  privileged.authenticatedElfSha256[0] = 0;
  assert(!invalid.acquire("cap.generic", 1).slot); // Host may not run Xtensa.
  assert(invalid.shutdown());

  GraphV2 malformed;
  privileged.authenticatedElfSha256[0] = 0xa5;
  privileged.requiredOsCpuAbi = 2;
  assert(!RuntimePackages::DeviceProviderExecutorV2::admitForFixture(malformed, privileged));
  privileged.requiredOsCpuAbi = 1;
  privileged.signedImports = duplicate;
  assert(!RuntimePackages::DeviceProviderExecutorV2::admitForFixture(malformed, privileged));
  privileged.signedImports = unsorted;
  assert(!RuntimePackages::DeviceProviderExecutorV2::admitForFixture(malformed, privileged));
  privileged.signedImports = nullptr;
  assert(!RuntimePackages::DeviceProviderExecutorV2::admitForFixture(malformed, privileged));
  privileged.signedImports = exact;
  privileged.signedImportCount = 129;
  assert(!RuntimePackages::DeviceProviderExecutorV2::admitForFixture(malformed, privileged));
  privileged.signedImportCount = 2;
  privileged.verifiedElfBytes = nullptr;
  assert(!RuntimePackages::DeviceProviderExecutorV2::admitForFixture(malformed, privileged));
  privileged.verifiedElfBytes = candidate;
  privileged.verifiedElfLength = 8u * 1024u * 1024u + 1u;
  assert(!RuntimePackages::DeviceProviderExecutorV2::admitForFixture(malformed, privileged));
  privileged.verifiedElfLength = sizeof(candidate);
  privileged.verifiedElfPath = "relative/driver.elf";
  assert(!RuntimePackages::DeviceProviderExecutorV2::admitForFixture(malformed, privileged));

  SpecV2 ordinary{"fixture-ordinary", "/sd/Drivers/ordinary/driver.elf",
                  "cap.other", 1, nullptr, 0};
  ordinary.authenticatedElfSha256[0] = 0xa5;
  assert(!malformed.addVerified(ordinary));
  ordinary.authenticatedElfSha256[0] = 0;
  ordinary.signedImports = exact;
  ordinary.signedImportCount = 2;
  assert(!malformed.addVerified(ordinary));
  ordinary.signedImports = nullptr;
  ordinary.signedImportCount = 0;
  ordinary.verifiedElfPath = nullptr;
  assert(!malformed.addVerified(ordinary));
  ordinary.verifiedElfPath = "/sd/Drivers/ordinary/driver.elf";
  assert(!RuntimePackages::DeviceProviderExecutorV2::admitForFixture(malformed, ordinary));
  assert(malformed.addVerified(ordinary));
  assert(malformed.shutdown());
  std::puts("Provider admission: public forged privileged specs denied, private metadata checks and host privilege denial PASS");
}
