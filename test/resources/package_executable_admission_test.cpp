#define HAL_STORAGE_IMPL
#include <HalStorage.h>
#include <SdFat.h>

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "runtime/packages/PackageExecutableAdmission.h"
#include "runtime/packages/PackageOrdinaryManifest.h"
#include "runtime/packages/PackageUseGate.h"
#include "runtime/packages/PackageVerificationReceiptSd.h"
using namespace RuntimePackages;
namespace RuntimePackages {
bool newPackageReceiptGeneration(uint8_t (&out)[16]) {
  static uint8_t next = 1;
  std::memset(out, next++, 16);
  return true;
}
}  // namespace RuntimePackages
std::string hex(const uint8_t* bytes) {
  const char* digits = "0123456789abcdef";
  std::string out;
  for (size_t i = 0; i < 32; ++i) {
    out += digits[bytes[i] >> 4];
    out += digits[bytes[i] & 15];
  }
  return out;
}
int runPackageExecutableAdmissionTest(std::vector<uint8_t>* retained = nullptr) {
  assert(Storage.begin());
  assert(Storage.mkdir("/Services"));
  assert(Storage.mkdir("/Services/clock"));
  Identity identity{};
  assert(makeIdentity(Kind::Service, "clock", "1.0.0", "driver.elf", false, &identity));
  std::vector<uint8_t> bytes(8192, 0x42);
  bytes[0] = 0x7f;
  bytes[1] = 'E';
  bytes[2] = 'L';
  bytes[3] = 'F';
  bytes[4] = 1;
  bytes[5] = 1;
  bytes[6] = 1;
  bytes[16] = 3;
  bytes[17] = 0;
  bytes[18] = 94;
  bytes[19] = 0;
  bytes[20] = 1;
  bytes[21] = bytes[22] = bytes[23] = 0;
  uint8_t elfDigest[32]{}, manifestDigest[32]{};
  assert(packageSnapshotDigest(bytes.data(), bytes.size(), elfDigest));
  const std::string manifest =
      "{\"schema\":1,\"kind\":\"service\",\"id\":\"clock\",\"version\":\"1.0.0\",\"artifact\":\"driver.elf\","
      "\"architecture\":\"xtensa-esp32s3\",\"min_runtime_api\":2,\"entries\":[{\"name\":\"driver.elf\",\"size_bytes\":"
      "8192,\"sha256\":\"" +
      hex(elfDigest) + "\",\"executable\":true}],\"requires\":[]}";
  OrdinaryPackagePlan plan{};
  assert(parseOrdinaryManifest(manifest.data(), manifest.size(), plan));
  assert(packageSnapshotDigest(reinterpret_cast<const uint8_t*>(manifest.data()), manifest.size(), manifestDigest));
  assert(Storage.writeFile("/Services/clock/.package.json", manifest));
  assert(writeVerifiedStageReceipt("/Services/clock", plan, reinterpret_cast<const uint8_t*>(manifest.data()),
                                   manifest.size()) == ReceiptWriteResult::Complete);
  auto& gate = systemPackageUseGate();
  assert(gate.pin("/Services/clock"));
  auto stamp = Storage.generation();
  receiptTestHashBytes = 0;
  assert(admitInstalledExecutableSnapshot(identity, manifestDigest, elfDigest, bytes.data(), bytes.size(), stamp));
  const size_t cold = receiptTestHashBytes;
  assert(cold >= bytes.size());  // receipt never grants cold authority
  receiptTestHashBytes = 0;
  assert(admitInstalledExecutableSnapshot(identity, manifestDigest, elfDigest, bytes.data(), bytes.size(), stamp));
  assert(receiptTestHashBytes < cold && receiptTestHashBytes < bytes.size());  // only bounded metadata SHA
  Storage.externalStorageBegin();
  receiptTestHashBytes = 0;
  assert(admitInstalledExecutableSnapshot(identity, manifestDigest, elfDigest, bytes.data(), bytes.size(),
                                          Storage.generation()));
  assert(receiptTestHashBytes >= bytes.size());  // working raw caller, no reuse
  Storage.externalStorageEnd(true);
  assert(Storage.reconcileExternalStorage());
  // A valid old snapshot must not be promoted with a freshly sampled epoch.
  stamp = Storage.generation();
  assert(Storage.writeFile("/interleaved", "mutation"));
  assert(admitInstalledExecutableSnapshot(identity, manifestDigest, elfDigest, bytes.data(), bytes.size(), stamp));
  bytes.back() ^= 1;
  assert(!admitInstalledExecutableSnapshot(identity, manifestDigest, elfDigest, bytes.data(), bytes.size(),
                                           Storage.generation()));
  bytes.back() ^= 1;
  manifestDigest[0] ^= 1;
  assert(!admitInstalledExecutableSnapshot(identity, manifestDigest, elfDigest, bytes.data(), bytes.size(),
                                           Storage.generation()));
  manifestDigest[0] ^= 1;
  std::string future = manifest;
  const auto policyAt = future.find("\"min_runtime_api\":2");
  assert(policyAt != std::string::npos);
  future.replace(policyAt, std::strlen("\"min_runtime_api\":2"), "\"min_runtime_api\":3");
  uint8_t futureDigest[32]{};
  assert(packageSnapshotDigest(reinterpret_cast<const uint8_t*>(future.data()), future.size(), futureDigest));
  assert(Storage.writeFile("/Services/clock/.package.json", future));
  assert(!admitInstalledExecutableSnapshot(identity, futureDigest, elfDigest, bytes.data(), bytes.size(),
                                           Storage.generation()));
  assert(Storage.writeFile("/Services/clock/.package.json", manifest));
  // Corrupt/replayed local receipt cannot create a warm trust shortcut.
  assert(Storage.writeFile("/Services/clock/.package.receipt", "forged"));
  receiptTestHashBytes = 0;
  assert(admitInstalledExecutableSnapshot(identity, manifestDigest, elfDigest, bytes.data(), bytes.size(),
                                          Storage.generation()));
  assert(receiptTestHashBytes >= bytes.size());
  receiptTestHashBytes = 0;
  assert(admitInstalledExecutableSnapshot(identity, manifestDigest, elfDigest, bytes.data(), bytes.size(),
                                          Storage.generation()));
  assert(receiptTestHashBytes >= bytes.size());
  assert(Storage.remove("/Services/clock/.package.receipt"));
  receiptTestHashBytes = 0;
  assert(admitInstalledExecutableSnapshot(identity, manifestDigest, elfDigest, bytes.data(), bytes.size(),
                                          Storage.generation()));
  assert(receiptTestHashBytes >= bytes.size());  // older no-receipt canonical package remains supported
  assert(gate.unpin("/Services/clock"));
  assert(!admitInstalledExecutableSnapshot(identity, manifestDigest, elfDigest, bytes.data(), bytes.size(),
                                           Storage.generation()));
  if (retained) *retained = bytes;
  puts(
      "Owned executable admission: cold actual-byte SHA, warm metadata-only proof, raw compatibility, stale-source "
      "epoch refusal, forged receipt and legacy absence PASS");
  return 0;
}
#ifndef PACKAGE_ADMISSION_NO_MAIN
int main() { return runPackageExecutableAdmissionTest(); }
#endif
