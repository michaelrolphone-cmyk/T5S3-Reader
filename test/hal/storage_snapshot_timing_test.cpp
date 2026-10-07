#define HAL_STORAGE_IMPL
#include <HalStorage.h>
#include <SdFat.h>
#include <Logging.h>
#include <freertos/task.h>
#include <cassert>
#include <cstdio>
#include <string>
#include "runtime/packages/InstalledCapabilityResolver.h"
#include "runtime/packages/PackageOrdinarySdAdapter.h"
#include "runtime/packages/PackageOrdinaryManifest.h"

using namespace RuntimePackages;
namespace {
unsigned readCostMs = 250;
void advanceRead(const std::string&, size_t) { FakeInventoryTime::ticks += readCostMs; }
void install(unsigned index) {
  const std::string id = index == 7 ? "battery" : index == 8 ? "rtc" : "provider" + std::to_string(index);
  const std::string path = "/Drivers/" + id;
  const std::string capability = index == 7 ? "board.battery" : index == 8 ? "rtc.clock" : "test.provider" + std::to_string(index);
  const std::string api = index == 8 ? "2" : "1";
  const std::string hash(64, 'a');
  const std::string profile = "os-cpu-abi=1\nprovides=" + capability + "\napi=" + api + "\n";
  const std::string manifest = "{\"schema\":1,\"kind\":\"driver\",\"id\":\"" + id +
      "\",\"version\":\"1.0.0\",\"artifact\":\"driver.elf\",\"architecture\":\"xtensa-esp32s3\","
      "\"min_runtime_api\":2,\"entries\":[{\"name\":\"driver.elf\",\"size_bytes\":64,\"sha256\":\"" + hash +
      "\",\"executable\":true},{\"name\":\"provider-abi.v1\",\"size_bytes\":" + std::to_string(profile.size()) +
      ",\"sha256\":\"" + hash + "\",\"executable\":false}],\"requires\":[]}";
  if (!Storage.exists(path.c_str())) assert(Storage.mkdir(path.c_str()));
  assert(Storage.writeFile((path + "/.package.json").c_str(), manifest));
  assert(Storage.writeFile((path + "/provider-abi.v1").c_str(), profile));
}
}
namespace RuntimePackages {
bool inspectInstalledOrdinarySdDirectory(const char* path, const PackageRuntimePolicy&, uint32_t (*)(const char*), Identity& observed, OrdinaryPackagePlan* plan, OrdinaryInspectionDiagnostic*) {
  auto manifest = Storage.open((std::string(path) + "/.package.json").c_str());
  if (!manifest.isOpen()) return false;
  std::string bytes(manifest.fileSize64(), '\0');
  const bool parsed = plan && manifest.read(bytes.data(), bytes.size()) == static_cast<int>(bytes.size()) &&
      parseOrdinaryManifest(bytes.data(), bytes.size(), *plan);
  if (!manifest.close() || !parsed) return false;
  return makeIdentity(Kind::Driver, std::strrchr(path, '/') + 1, "1.0.0", "driver.elf", false, &observed);
}
}
int main() {
  assert(Storage.begin() && Storage.mkdir("/Drivers"));
  for (unsigned i = 0; i < 9; ++i) install(i);
  FakeSd::afterRead = advanceRead;
  const auto began = FakeInventoryTime::ticks;
  auto* slow = captureInstalledCapabilities();
  const auto elapsed = FakeInventoryTime::ticks - began;
  assert(elapsed > 2000 && elapsed < 15000 && FakeInventoryTime::yields >= 9);
  assert(slow && versionInInstalledSnapshot(slow, "board.battery") == 1 && versionInInstalledSnapshot(slow, "rtc.clock") == 2);
  const unsigned reads = FakeSd::reads, opens = FakeSd::opens;
  auto* warm = captureInstalledCapabilities();
  assert(warm && FakeSd::reads == reads && FakeSd::opens == opens);
  releaseInstalledCapabilities(warm);
  // A genuinely over-budget scan still terminates and never enters reuse.
  Storage.invalidateObservations();
  assert(!versionInInstalledSnapshot(slow, "board.battery"));
  releaseInstalledCapabilities(slow);
  readCostMs = 1000;
  const auto overBegan = FakeInventoryTime::ticks;
  assert(!captureInstalledCapabilities());
  const auto overElapsed = FakeInventoryTime::ticks - overBegan;
  assert(overElapsed >= 15000 && overElapsed < 20000);
  const auto refusedReads = FakeSd::reads;
  readCostMs = 250;
  auto* retry = captureInstalledCapabilities();
  assert(retry && FakeSd::reads > refusedReads && versionInInstalledSnapshot(retry, "rtc.clock") == 2);
  releaseInstalledCapabilities(retry);
  // Optional RTC absence and malformed metadata cannot poison the battery.
  assert(Storage.remove("/Drivers/rtc/.package.json") && Storage.remove("/Drivers/rtc/provider-abi.v1") && Storage.rmdir("/Drivers/rtc"));
  auto* absent = captureInstalledCapabilities();
  assert(absent && !versionInInstalledSnapshot(absent, "rtc.clock") && versionInInstalledSnapshot(absent, "board.battery") == 1);
  releaseInstalledCapabilities(absent);
  install(8);
  assert(Storage.writeFile("/Drivers/rtc/provider-abi.v1", "invalid profile\n"));
  auto* malformed = captureInstalledCapabilities();
  assert(malformed && !versionInInstalledSnapshot(malformed, "rtc.clock") && versionInInstalledSnapshot(malformed, "board.battery") == 1);
  assert(FakeStorageLog::contains("path=/Drivers/rtc stage=provider-profile"));
  releaseInstalledCapabilities(malformed);
  const unsigned malformedReads = FakeSd::reads;
  auto* stillMalformed = captureInstalledCapabilities();
  assert(stillMalformed && FakeSd::reads > malformedReads); // no indefinite cached omission
  releaseInstalledCapabilities(stillMalformed);
  install(8);
  auto* restored = captureInstalledCapabilities();
  assert(restored && versionInInstalledSnapshot(restored, "rtc.clock") == 2);
  releaseInstalledCapabilities(restored);
  Storage.invalidateObservations();
  FakeSd::failDirectory = "/drivers";
  assert(!captureInstalledCapabilities());
  FakeSd::failDirectory.clear();
  FakeSd::afterRead = nullptr;
  std::printf("Capability inventory: nine providers at %u ms, zero-I/O warm reuse, %u ms deadline refusal, retry, optional RTC, malformed and directory fault PASS\n", elapsed, overElapsed);
}
