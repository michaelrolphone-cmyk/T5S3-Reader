#include "runtime/drivers/ProviderGraphV2.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <type_traits>

using RuntimeProviders::GraphV2;
using RuntimeProviders::SpecV2;

// Friend test fixture models ONLY the compiler-visible firmware manager
// boundary. Package integrity and independent manager admission remain
// separate; no manifest or digest alone confers privileged execution.
namespace RuntimePackages {
class DeviceProviderExecutorV2 {
 public:
  static bool admitForFixture(GraphV2& graph, const SpecV2& spec) {
    return graph.addManagerValidatedPrivileged(spec);
  }
};
}

static_assert(std::is_array<decltype(SpecV2::contentSha256)>::value,
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
  privileged.contentSha256[0] = 0xa5;
  assert(!invalid.addVerified(privileged));
  assert(!RuntimePackages::DeviceProviderExecutorV2::admitForFixture(invalid, privileged));
  privileged.declaredImports = exact;
  privileged.declaredImportCount = 2;
  assert(!invalid.addVerified(privileged)); // Even plausible hash+imports are NOT signing.
  assert(RuntimePackages::DeviceProviderExecutorV2::admitForFixture(invalid, privileged));
  assert(invalid.moduleCount() == 1);
  privileged.contentSha256[0] = 0;
  assert(!invalid.acquire("cap.generic", 1).slot); // Host may not run Xtensa.
  assert(invalid.shutdown());

  GraphV2 revision2;
  privileged.requiredOsCpuAbi=2;
  assert(!revision2.addVerified(privileged));
  assert(RuntimePackages::DeviceProviderExecutorV2::admitForFixture(revision2,privileged));
  assert(!revision2.acquire("cap.generic",1).slot);
  assert(revision2.shutdown());

  GraphV2 malformed;
  privileged.contentSha256[0] = 0xa5;
  privileged.requiredOsCpuAbi = 3;
  assert(!RuntimePackages::DeviceProviderExecutorV2::admitForFixture(malformed, privileged));
  privileged.requiredOsCpuAbi = 1;
  privileged.declaredImports = duplicate;
  assert(!RuntimePackages::DeviceProviderExecutorV2::admitForFixture(malformed, privileged));
  privileged.declaredImports = unsorted;
  assert(!RuntimePackages::DeviceProviderExecutorV2::admitForFixture(malformed, privileged));
  privileged.declaredImports = nullptr;
  assert(!RuntimePackages::DeviceProviderExecutorV2::admitForFixture(malformed, privileged));
  privileged.declaredImports = exact;
  privileged.declaredImportCount = 129;
  assert(!RuntimePackages::DeviceProviderExecutorV2::admitForFixture(malformed, privileged));
  privileged.declaredImportCount = 2;
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
  ordinary.contentSha256[0] = 0xa5;
  assert(!malformed.addVerified(ordinary));
  ordinary.contentSha256[0] = 0;
  ordinary.declaredImports = exact;
  ordinary.declaredImportCount = 2;
  assert(!malformed.addVerified(ordinary));
  ordinary.declaredImports = nullptr;
  ordinary.declaredImportCount = 0;
  ordinary.verifiedElfPath = nullptr;
  assert(!malformed.addVerified(ordinary));
  ordinary.verifiedElfPath = "/sd/Drivers/ordinary/driver.elf";
  assert(!RuntimePackages::DeviceProviderExecutorV2::admitForFixture(malformed, ordinary));
  assert(malformed.addVerified(ordinary));
  assert(malformed.shutdown());
  std::puts("Provider admission: public forged privileged specs denied, private metadata checks and host privilege denial PASS");
}
