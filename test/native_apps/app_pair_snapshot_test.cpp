#define HAL_STORAGE_IMPL
#include <HalStorage.h>
#include <SdFat.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "native/AppManifest.h"
#include "native/AppPackageInstaller.h"
#include "runtime/packages/PackageExecutableAdmission.h"
#include "runtime/packages/PackageVerificationReceipt.h"
uint32_t fakeTime = 0;
unsigned pairTestYields = 0;
extern "C" const char* native_app_current_path() { return nullptr; }
static std::string alternate;
static void changeAfterClose(const std::string& path) {
  if (path == "/apps/test.json") {
    FakeSd::nodes[path]->bytes = alternate;
    FakeSd::afterClose = nullptr;
  }
}
static void slowRead(const std::string& path, size_t) {
  if (path == "/apps/test.elf") fakeTime += 1001;
}
static void invalidateDuringHash() {
  receiptTestHashHook = nullptr;
  Storage.invalidateObservations();
}
static std::string hexDigest(const std::string& bytes) {
  uint8_t digest[32]{};
  assert(mbedtls_sha256_ret(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), digest, 0) == 0);
  std::string out;
  for (const auto value : digest) {
    out += "0123456789abcdef"[value >> 4];
    out += "0123456789abcdef"[value & 15];
  }
  return out;
}
static std::string load(const std::filesystem::path& path, uint64_t maximum) {
  assert(!std::filesystem::is_symlink(path) && std::filesystem::is_regular_file(path));
  const auto count = std::filesystem::file_size(path);
  assert(count && count <= maximum);
  std::string bytes(static_cast<size_t>(count), '\0');
  std::ifstream input(path, std::ios::binary);
  assert(input.read(bytes.data(), bytes.size()));
  return bytes;
}
static int measure(const char* directory) {
  assert(Storage.begin());
  FakeSd::nodes["/apps"] = std::make_shared<FakeSd::Node>(FakeSd::Node{true, {}});
  std::vector<std::string> names;
  uint64_t totalBytes = 0;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (entry.path().extension() != ".elf") continue;
    assert(names.size() < 128);
    const auto name = entry.path().filename().string();
    auto bytes = load(entry.path(), 8u * 1024u * 1024u);
    totalBytes += bytes.size();
    assert(totalBytes <= 128u * 1024u * 1024u);
    auto sidecar = entry.path();
    sidecar.replace_extension(".json");
    FakeSd::nodes[FakeSd::path(("/Apps/" + name).c_str())] =
        std::make_shared<FakeSd::Node>(FakeSd::Node{false, std::move(bytes)});
    FakeSd::nodes[FakeSd::path(("/Apps/" + sidecar.filename().string()).c_str())] =
        std::make_shared<FakeSd::Node>(FakeSd::Node{false, load(sidecar, 2048)});
    names.push_back(name);
  }
  assert(!names.empty());
  std::sort(names.begin(), names.end());
  struct Sample {
    uint64_t micros, hashed, bytes;
    unsigned opens, reads;
  } samples[2]{};
  for (unsigned mode = 0; mode < 2; ++mode) {
    FakeSd::opens = FakeSd::reads = 0;
    FakeSd::bytesRead = receiptTestHashBytes = 0;
    const auto started = std::chrono::steady_clock::now();
    for (const auto& name : names) {
      const std::string elf = "/Apps/" + name;
      const std::string json = elf.substr(0, elf.size() - 4) + ".json";
      assert(mode ? RuntimePackages::inspectInstalledAppPair(elf.c_str(), json.c_str(), name.c_str())
                  : RuntimePackages::verifyAppPair(elf.c_str(), json.c_str(), name.c_str(), true));
    }
    samples[mode] = {
        static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count()),
        receiptTestHashBytes, FakeSd::bytesRead, FakeSd::opens, FakeSd::reads};
  }
  assert(samples[0].hashed == totalBytes && samples[1].hashed == 0);
  assert(samples[1].reads == names.size() * 2 && samples[1].opens == names.size() * 2);
  std::printf(
      "{\"schema\":1,\"packages\":%u,\"elf_bytes\":%llu,\"full_verifier\":{\"host_us\":%llu,\"hashed_bytes\":%llu,"
      "\"read_bytes\":%llu,\"opens\":%u,\"reads\":%u},"
      "\"installed_inspection\":{\"host_us\":%llu,\"hashed_bytes\":%llu,\"read_bytes\":%llu,\"opens\":%u,\"reads\":%u}}"
      "\n",
      static_cast<unsigned>(names.size()), static_cast<unsigned long long>(totalBytes),
      static_cast<unsigned long long>(samples[0].micros), static_cast<unsigned long long>(samples[0].hashed),
      static_cast<unsigned long long>(samples[0].bytes), samples[0].opens, samples[0].reads,
      static_cast<unsigned long long>(samples[1].micros), static_cast<unsigned long long>(samples[1].hashed),
      static_cast<unsigned long long>(samples[1].bytes), samples[1].opens, samples[1].reads);
  return 0;
}
int main(int argc, char** argv) {
  if (argc == 2) return measure(argv[1]);
  assert(Storage.begin());
  const std::string prefix =
      "{\"display_name\":\"Test\",\"file_name\":\"test.elf\",\"version\":\"1.0.0\",\"min_firmware_version\":\"1.0.0\","
      "\"icon\":\"solid:f02d\"";
  std::string elf(65536, 'A');
  elf[0] = 0x7f;
  elf[1] = 'E';
  elf[2] = 'L';
  elf[3] = 'F';
  elf[4] = elf[5] = 1;
  elf[16] = 3;
  elf[17] = 0;
  elf[18] = 94;
  elf[19] = 0;
  const auto digest = hexDigest(elf);
  const auto manifest = prefix + ",\"size_bytes\":65536,\"sha256\":\"" + digest + "\"}";
  FakeSd::nodes["/apps"] = std::make_shared<FakeSd::Node>(FakeSd::Node{true, {}});
  FakeSd::nodes["/apps/test.elf"] = std::make_shared<FakeSd::Node>(FakeSd::Node{false, elf});
  FakeSd::nodes["/apps/test.json"] = std::make_shared<FakeSd::Node>(FakeSd::Node{false, manifest});
  // The actual parser must consume the bytes read before close, not a reopened path.
  alternate = prefix + ",\"size_bytes\":65536,\"sha256\":\"" + std::string(64, '0') + "\"}";
  FakeSd::afterClose = changeAfterClose;
  t5_app_manifest_t parsed{};
  AppIntegrity integrity{};
  assert(readAppManifest("/Apps/test.json", parsed, nullptr, false, nullptr, nullptr, &integrity));
  assert(integrity.present && digest == integrity.sha256 && FakeSd::nodes["/apps/test.json"]->bytes == alternate);
  FakeSd::nodes["/apps/test.json"]->bytes = manifest;
  FakeSd::afterClose = changeAfterClose;
  receiptTestHashBytes = 0;
  assert(RuntimePackages::verifyAppPair("/Apps/test.elf", "/Apps/test.json", "test.elf", true));
  assert(receiptTestHashBytes == elf.size() && pairTestYields > 0);
  // A subsequent operation sees the changed metadata and refuses its mismatch.
  assert(!RuntimePackages::verifyAppPair("/Apps/test.elf", "/Apps/test.json", "test.elf", true));
  FakeSd::nodes["/apps/test.json"]->bytes = manifest;
  const auto hashBefore = receiptTestHashBytes;
  const auto readsBefore = FakeSd::reads;
  assert(RuntimePackages::inspectInstalledAppPair("/Apps/test.elf", "/Apps/test.json", "test.elf"));
  assert(receiptTestHashBytes == hashBefore);  // normal inventory does not hash ELF
  assert(FakeSd::reads == readsBefore + 2);    // one sidecar buffer plus the fixed ELF header
  FakeSd::nodes["/apps/test.elf"]->bytes.pop_back();
  assert(!RuntimePackages::inspectInstalledAppPair("/Apps/test.elf", "/Apps/test.json", "test.elf"));
  FakeSd::nodes["/apps/test.elf"]->bytes = elf;
  FakeSd::nodes["/apps/test.elf"]->bytes[0] = 0;
  assert(!RuntimePackages::inspectInstalledAppPair("/Apps/test.elf", "/Apps/test.json", "test.elf"));
  FakeSd::nodes["/apps/test.elf"]->bytes = elf;
  assert(receiptTestHashBytes == hashBefore);
  uint8_t capturedSidecar[32]{}, expectedElf[32]{};
  assert(mbedtls_sha256_ret(reinterpret_cast<const uint8_t*>(manifest.data()), manifest.size(), capturedSidecar, 0) ==
         0);
  assert(RuntimePackages::receiptDigest(digest.c_str(), expectedElf));
  const auto originalStamp = Storage.generation();
  assert(originalStamp.quiescent);
  assert(RuntimePackages::admitLooseExecutableSnapshot("/sd/Apps/test.elf", capturedSidecar, expectedElf,
                                                       reinterpret_cast<const uint8_t*>(elf.data()), elf.size(),
                                                       originalStamp));
  FakeSd::nodes["/apps/test.elf"]->bytes[35] ^= 1;  // simulated unobserved external edit
  assert(!RuntimePackages::verifyAppPair("/Apps/test.elf", "/Apps/test.json", "test.elf", true));
  assert(!Storage.unchanged(originalStamp));
  const auto& changed = FakeSd::nodes["/apps/test.elf"]->bytes;
  assert(!RuntimePackages::admitLooseExecutableSnapshot("/sd/Apps/test.elf", capturedSidecar, expectedElf,
                                                        reinterpret_cast<const uint8_t*>(changed.data()),
                                                        changed.size(), originalStamp));
  // An in-flight old, actually valid copy can still pass cold SHA, but must not
  // acquire the new storage epoch and seed a later shortcut for changed bytes.
  assert(RuntimePackages::admitLooseExecutableSnapshot("/sd/Apps/test.elf", capturedSidecar, expectedElf,
                                                       reinterpret_cast<const uint8_t*>(elf.data()), elf.size(),
                                                       originalStamp));
  assert(!RuntimePackages::admitLooseExecutableSnapshot("/sd/Apps/test.elf", capturedSidecar, expectedElf,
                                                        reinterpret_cast<const uint8_t*>(changed.data()),
                                                        changed.size(), Storage.generation()));
  FakeSd::nodes["/apps/test.elf"]->bytes = elf;
  const auto beforeSuccess = Storage.generation();
  assert(RuntimePackages::verifyAppPair("/Apps/test.elf", "/Apps/test.json", "test.elf", true));
  assert(!Storage.unchanged(beforeSuccess) && Storage.ready());
  const auto fresh = Storage.generation();
  assert(RuntimePackages::admitLooseExecutableSnapshot("/sd/Apps/test.elf", capturedSidecar, expectedElf,
                                                       reinterpret_cast<const uint8_t*>(elf.data()), elf.size(),
                                                       fresh));
  const auto warmHash = receiptTestHashBytes;
  assert(RuntimePackages::admitLooseExecutableSnapshot("/sd/Apps/test.elf", capturedSidecar, expectedElf,
                                                       reinterpret_cast<const uint8_t*>(elf.data()), elf.size(),
                                                       fresh));
  assert(receiptTestHashBytes == warmHash);
  Storage.invalidateObservations();
  const auto beforeInFlight = Storage.generation();
  receiptTestHashHook = invalidateDuringHash;
  assert(RuntimePackages::admitLooseExecutableSnapshot("/sd/Apps/test.elf", capturedSidecar, expectedElf,
                                                       reinterpret_cast<const uint8_t*>(elf.data()), elf.size(),
                                                       beforeInFlight));
  assert(!Storage.unchanged(beforeInFlight) && receiptTestHashHook == nullptr);
  std::string laterChanged = elf;
  laterChanged[35] ^= 1;
  assert(!RuntimePackages::admitLooseExecutableSnapshot("/sd/Apps/test.elf", capturedSidecar, expectedElf,
                                                        reinterpret_cast<const uint8_t*>(laterChanged.data()),
                                                        laterChanged.size(), Storage.generation()));
  FakeSd::failClose = "/apps/test.json";
  assert(!readAppManifest("/Apps/test.json", parsed, nullptr, false, nullptr, nullptr, &integrity) &&
         !integrity.present);
  assert(!RuntimePackages::verifyAppPair("/Apps/test.elf", "/Apps/test.json", "test.elf", true));
  FakeSd::failClose = "/apps/test.elf";
  assert(!RuntimePackages::verifyAppPair("/Apps/test.elf", "/Apps/test.json", "test.elf", true));
  FakeSd::failClose.clear();
  FakeSd::afterRead = slowRead;
  const auto beforeSlow = receiptTestHashBytes;
  assert(!RuntimePackages::verifyAppPair("/Apps/test.elf", "/Apps/test.json", "test.elf", true));
  assert(receiptTestHashBytes - beforeSlow < elf.size());
  FakeSd::afterRead = nullptr;
  FakeSd::nodes["/apps/test.json"]->bytes = prefix + "}";
  assert(RuntimePackages::verifyAppPair("/Apps/test.elf", "/Apps/test.json", "test.elf", false));
  assert(!RuntimePackages::verifyAppPair("/Apps/test.elf", "/Apps/test.json", "test.elf", true));
  assert(!Storage.generation().quiescent);  // discarded close failures remain uncertain
  puts(
      "Actual legacy pair adapter: one sidecar snapshot, changed-path isolation, checked close, full SHA/deadline and "
      "explicit-check invalidation and metadata-only inventory PASS");
}
