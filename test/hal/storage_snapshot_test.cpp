#define HAL_STORAGE_IMPL
#include <HalStorage.h>
#include <SdFat.h>

#include <cassert>
#include <iostream>

#include "runtime/packages/InstalledCapabilityResolver.h"
#include "runtime/packages/PackageOrdinarySdAdapter.h"
using namespace RuntimePackages;
namespace {
bool mutateDuringInspect = false;
}
namespace RuntimePackages {
bool inspectInstalledOrdinarySdDirectory(const char* path, const PackageRuntimePolicy&, uint32_t (*)(const char*),
                                         Identity& observed) {
  if (mutateDuringInspect) {
    mutateDuringInspect = false;
    assert(Storage.writeFile("/interleaved", "changed"));
  }
  if (!Storage.exists((std::string(path) + "/.package.json").c_str())) return false;
  const char* id = std::strrchr(path, '/') + 1;
  return makeIdentity(Kind::Driver, id, "1.0.0", "driver.elf", false, &observed);
}
}  // namespace RuntimePackages
int main() {
  assert(Storage.begin());
  assert(Storage.mkdir("/Drivers"));
  assert(Storage.mkdir("/Drivers/clock"));
  const std::string hash(64, 'a');
  const std::string manifest =
      "{\"schema\":1,\"kind\":\"driver\",\"id\":\"clock\",\"version\":\"1.0.0\",\"artifact\":\"driver.elf\","
      "\"architecture\":\"xtensa-esp32s3\",\"min_runtime_api\":2,\"entries\":[{\"name\":\"driver.elf\",\"size_bytes\":"
      "64,\"sha256\":\"" +
      hash + "\",\"executable\":true},{\"name\":\"provider-abi.v1\",\"size_bytes\":41,\"sha256\":\"" + hash +
      "\",\"executable\":false}],\"requires\":[]}";
  assert(Storage.writeFile("/Drivers/clock/.package.json", manifest));
  assert(Storage.writeFile("/Drivers/clock/provider-abi.v1", "os-cpu-abi=1\nprovides=test.clock\napi=1\n"));
  auto* snapshot = captureInstalledCapabilities();
  assert(snapshot && versionInInstalledSnapshot(snapshot, "test.clock") == 1);
  Storage.externalStorageBegin();
  // Legacy capability admission remains usable during raw-compatibility access.
  // This operation-local metadata query is not a trusted coherent cache.
  auto* compatible = captureInstalledCapabilities();
  assert(compatible && versionInInstalledSnapshot(compatible, "test.clock") == 1);
  releaseInstalledCapabilities(compatible);
  Storage.externalStorageEnd(true);
  releaseInstalledCapabilities(snapshot);
  FakeSd::failDirectory = "/drivers";
  assert(!captureInstalledCapabilities());
  FakeSd::failDirectory.clear();
  for (unsigned i = 0; i < 64; ++i) assert(Storage.mkdir(("/Drivers/extra" + std::to_string(i)).c_str()));
  assert(!captureInstalledCapabilities());
  std::cout << "Production legacy capability snapshot: compatible raw session, directory I/O "
               "faults and overflow PASS\n";
}
