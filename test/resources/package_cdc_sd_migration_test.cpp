#include <HalStorage.h>

#include <cassert>
#include <iostream>

#include "runtime/packages/PackageCdcSdMigration.h"
#include "runtime/packages/PackageOrdinarySdAdapter.h"
using namespace RuntimePackages;
namespace {
constexpr PackageRuntimePolicy policy{"xtensa-esp32s3", 2, 8 * 1024 * 1024, 16 * 1024 * 1024};
uint32_t resolver(const char*) { return UINT32_MAX; }
void package(const char* path, const char* id, const char* version) {
  std::filesystem::create_directories(CdcSdTest::full(path));
  std::ofstream(CdcSdTest::full(path) + "/.package.json") << id << '\n' << version << '\n';
  std::ofstream(CdcSdTest::full(path) + "/driver.elf") << "verified fixture payload";
}
void reset() {
  std::filesystem::remove_all(CdcSdTest::root);
  std::filesystem::create_directories(CdcSdTest::root + "/Drivers");
  CdcSdTest::failClose.clear();
  CdcSdTest::failWrite.clear();
  CdcSdTest::cut = 0;
  CdcSdTest::renames = 0;
  package(kCdcAliasRoot, kCdcAliasId, "0.1.7");
  package("/Drivers/.usb-cdc-acm.pkg-stage", kCdcCanonicalId, "0.1.8");
}
OrdinaryTransactionResult publish() {
  Identity candidate{}, observed{};
  assert(makeIdentity(Kind::Driver, kCdcCanonicalId, "0.1.8", "driver.elf", false, &candidate));
  return publishCanonicalCdcFromSd(candidate, policy, resolver, observed);
}
}  // namespace
namespace RuntimePackages {
// Substitute only package verification/purge edges. Actual production SD
// intent serialization, SHA, transaction and filesystem calls run unchanged.
bool verifyManagedOrdinarySdDirectory(const char* path, Kind kind, const char* id, const PackageRuntimePolicy&,
                                      uint32_t (*)(const char*), Identity& out, bool) {
  const auto root = CdcSdTest::full(path);
  if (!std::filesystem::is_directory(root)) return false;
  size_t count = 0;
  for (auto const& entry : std::filesystem::directory_iterator(root)) {
    if (entry.path().filename() != ".package.json" && entry.path().filename() != "driver.elf") return false;
    ++count;
  }
  std::ifstream input(root + "/.package.json");
  std::string found, version;
  std::getline(input, found);
  std::getline(input, version);
  return count == 2 && found == id && makeIdentity(kind, id, version.c_str(), "driver.elf", false, &out);
}
bool purgeManagedOrdinarySdDirectory(const char* path, Kind, const char*) {
  const auto root = CdcSdTest::full(path);
  if (!std::filesystem::is_directory(root)) return false;
  // Mirror the shared helper's legacy fallback so the migration wrapper must
  // explicitly exclude it when its bound ordinary alias manifest is absent.
  const char* metadata = std::filesystem::exists(root + "/.package.json") ? ".package.json" : "manifest.json";
  for (auto const& entry : std::filesystem::directory_iterator(root))
    if (entry.path().filename() != metadata && entry.path().filename() != "driver.elf") return false;
  std::filesystem::remove(root + "/driver.elf");
  std::filesystem::remove(root + "/" + metadata);
  return std::filesystem::remove(root);
}
}  // namespace RuntimePackages
int main(int argc, char** argv) {
  assert(argc == 2);
  CdcSdTest::root = argv[1];
  reset();
  assert(publish() == OrdinaryTransactionResult::Published);
  assert(!cdcMigrationPendingOnSd());
  reset();
  package(kCdcCanonicalRoot, kCdcCanonicalId, "0.1.0");
  assert(publish() == OrdinaryTransactionResult::Published);
  assert(!cdcMigrationPendingOnSd() && !Storage.exists(kCdcAliasRoot));
  reset();
  CdcSdTest::failDirectoryRead = "/Drivers";
  assert(publish() == OrdinaryTransactionResult::AmbiguousState);
  assert(Storage.exists(kCdcAliasRoot) && CdcSdTest::renames == 0);
  CdcSdTest::failDirectoryRead.clear();
  reset();
  std::filesystem::rename(CdcSdTest::full(kCdcAliasRoot) + "/.package.json",
                          CdcSdTest::full(kCdcAliasRoot) + "/manifest.json");
  assert(publish() == OrdinaryTransactionResult::InvalidInstalled);
  assert(!cdcMigrationPendingOnSd() && CdcSdTest::renames == 0);
  for (int cut : {1, 2, 3}) {
    reset();
    CdcSdTest::cut = cut;
    try {
      (void)publish();
      assert(false);
    } catch (int) {
    }
    CdcSdTest::cut = 0;
    Identity observed{};
    assert(cdcMigrationPendingOnSd());
    auto r = reconcileCdcMigrationFromSd(policy, resolver, observed);
    assert(r == OrdinaryTransactionResult::InstalledVerified || r == OrdinaryTransactionResult::NoInstalledPackage);
    assert(!cdcMigrationPendingOnSd());
  }
  reset();
  CdcSdTest::failClose = kCdcIntentPart;
  assert(publish() == OrdinaryTransactionResult::RestorePending);
  CdcSdTest::failClose.clear();
  Identity observed{};
  assert(reconcileCdcMigrationFromSd(policy, resolver, observed) == OrdinaryTransactionResult::NoInstalledPackage);
  assert(Storage.exists(kCdcAliasRoot));
  reset();
  CdcSdTest::failWrite = kCdcIntentPart;
  assert(publish() == OrdinaryTransactionResult::RestorePending);
  CdcSdTest::failWrite.clear();
  assert(!cdcMigrationPendingOnSd());
  assert(reconcileCdcMigrationFromSd(policy, resolver, observed) == OrdinaryTransactionResult::NoInstalledPackage);
  assert(Storage.exists(kCdcAliasRoot));
  // A crash-created truncated part is removed only when every byte matches
  // the intent reconstructed from the same full alias and staged generation.
  for (bool corrupt : {false, true}) {
    reset();
    CdcSdTest::cut = 1;
    try {
      (void)publish();
    } catch (int) {
    }
    CdcSdTest::cut = 0;
    const auto part = CdcSdTest::full(kCdcIntentPart);
    std::filesystem::rename(CdcSdTest::full(kCdcIntentPath), part);
    std::filesystem::resize_file(part, 20);
    if (corrupt) std::ofstream(part, std::ios::binary) << "unrecognized user data";
    const auto result = reconcileCdcMigrationFromSd(policy, resolver, observed);
    assert(result ==
           (corrupt ? OrdinaryTransactionResult::AmbiguousState : OrdinaryTransactionResult::NoInstalledPackage));
    assert(cdcMigrationPendingOnSd() == corrupt && Storage.exists(kCdcAliasRoot));
  }
  reset();
  CdcSdTest::cut = 3;
  try {
    (void)publish();
  } catch (int) {
  }
  CdcSdTest::cut = 0;
  std::ofstream(CdcSdTest::full(kCdcHoldingRoot) + "/.package.json") << kCdcAliasId << "\n0.1.9\n";
  assert(reconcileCdcMigrationFromSd(policy, resolver, observed) == OrdinaryTransactionResult::CleanupPending);
  assert(Storage.exists(kCdcHoldingRoot) && cdcMigrationPendingOnSd());
  reset();
  CdcSdTest::cut = 3;
  try {
    (void)publish();
  } catch (int) {
  }
  CdcSdTest::cut = 0;
  std::filesystem::rename(CdcSdTest::full(kCdcHoldingRoot) + "/.package.json",
                          CdcSdTest::full(kCdcHoldingRoot) + "/manifest.json");
  assert(reconcileCdcMigrationFromSd(policy, resolver, observed) == OrdinaryTransactionResult::CleanupPending);
  assert(Storage.exists(kCdcHoldingRoot) && cdcMigrationPendingOnSd());
  std::filesystem::remove_all(CdcSdTest::root);
  std::cout << "Actual SD migration wrapper: intent SHA, serialization, rename restart, close/partial-write "
               "preservation PASS\n";
}
