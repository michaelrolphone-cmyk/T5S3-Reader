#define HAL_STORAGE_IMPL
#include <HalStorage.h>
#include <SdFat.h>

#include <cassert>
#include <iostream>

#include "runtime/packages/InstalledCapabilityResolver.h"
#include "runtime/packages/PackageOrdinarySdAdapter.h"
#include "runtime/packages/PackageOrdinaryManifest.h"
using namespace RuntimePackages;
namespace {
bool mutateDuringInspect = false;
bool refuseInspectionOnce = false;
}
namespace RuntimePackages {
bool inspectInstalledOrdinarySdDirectory(const char* path, const PackageRuntimePolicy&, uint32_t (*)(const char*),
                                         Identity& observed, OrdinaryPackagePlan* plan) {
  if (refuseInspectionOnce) { refuseInspectionOnce = false; return false; }
  if (mutateDuringInspect) {
    mutateDuringInspect = false;
    assert(Storage.writeFile("/interleaved", "changed"));
  }
  auto manifest = Storage.open((std::string(path) + "/.package.json").c_str());
  if (!manifest.isOpen()) return false;
  std::string bytes(manifest.fileSize64(), '\0');
  const bool parsed = plan && manifest.read(bytes.data(), bytes.size()) == static_cast<int>(bytes.size()) &&
      parseOrdinaryManifest(bytes.data(), bytes.size(), *plan);
  if (!manifest.close() || !parsed) return false;
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
  const unsigned opened = FakeSd::opens, read = FakeSd::reads;
  auto* warm = captureInstalledCapabilities();
  assert(warm && versionInInstalledSnapshot(warm, "test.clock") == 1);
  assert(FakeSd::opens == opened && FakeSd::reads == read); // zero SD scans/hash IO
  releaseInstalledCapabilities(warm);
  assert(Storage.writeFile("/changed", "x"));
  assert(!versionInInstalledSnapshot(snapshot, "test.clock"));
  auto* refreshed = captureInstalledCapabilities();
  assert(refreshed && versionInInstalledSnapshot(refreshed, "test.clock") == 1);
  assert(FakeSd::opens > opened && FakeSd::reads > read);
  releaseInstalledCapabilities(refreshed);
  assert(Storage.writeFile("/changed", "transient"));
  refuseInspectionOnce = true;
  auto* omitted = captureInstalledCapabilities();
  assert(omitted && !versionInInstalledSnapshot(omitted, "test.clock"));
  releaseInstalledCapabilities(omitted);
  auto* retry = captureInstalledCapabilities();
  assert(retry && versionInInstalledSnapshot(retry, "test.clock") == 1);
  releaseInstalledCapabilities(retry); // omitted metadata was not retained
  assert(Storage.writeFile("/changed", "y"));
  mutateDuringInspect = true;
  assert(!captureInstalledCapabilities()); // changing scan never enters coherent reuse
  auto* settled = captureInstalledCapabilities();assert(settled);
  Storage.externalStorageBegin();
  assert(!versionInInstalledSnapshot(settled, "test.clock"));
  releaseInstalledCapabilities(settled);
  // Legacy capability admission remains usable during raw-compatibility access.
  // This operation-local metadata query is not a trusted coherent cache.
  auto* compatible = captureInstalledCapabilities();
  assert(compatible && versionInInstalledSnapshot(compatible, "test.clock") == 1);
  const unsigned rawOpens = FakeSd::opens;
  auto* compatibleAgain = captureInstalledCapabilities();
  assert(compatibleAgain && FakeSd::opens > rawOpens);
  releaseInstalledCapabilities(compatibleAgain);
  releaseInstalledCapabilities(compatible);
  Storage.externalStorageEnd(true);
  releaseInstalledCapabilities(snapshot);
  assert(Storage.reconcileExternalStorage());
  auto* remounted = captureInstalledCapabilities();assert(remounted);
  assert(Storage.begin());
  assert(!versionInInstalledSnapshot(remounted, "test.clock"));
  releaseInstalledCapabilities(remounted);
  auto* beforeFailure = captureInstalledCapabilities();assert(beforeFailure);
  FakeSd::mountOkay = false;assert(!Storage.begin());
  assert(!versionInInstalledSnapshot(beforeFailure, "test.clock"));
  assert(!captureInstalledCapabilities());releaseInstalledCapabilities(beforeFailure);
  FakeSd::mountOkay = true;assert(Storage.begin());
  FakeSd::failDirectory = "/drivers";
  assert(!captureInstalledCapabilities());
  FakeSd::failDirectory.clear();
  for (unsigned i = 0; i < 64; ++i) assert(Storage.mkdir(("/Drivers/extra" + std::to_string(i)).c_str()));
  assert(!captureInstalledCapabilities());
  std::cout << "Production capability metadata cache: zero-IO reuse, mutation/remount invalidation, interleaving, uncached compatible raw session, directory I/O "
               "faults and overflow PASS\n";
}
