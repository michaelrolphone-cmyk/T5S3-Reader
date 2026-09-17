#include "runtime/packages/PackageSignedTransaction.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <utility>

using namespace RuntimePackages;
namespace {
struct Store {
  std::map<std::pair<unsigned, std::string>, uint32_t> values;
  bool unavailable = false;
  uint32_t failSecurityVersion = 0;
  std::mutex mutex;
  FloorRead read(Kind kind, const char* id, uint32_t& out) {
    std::lock_guard<std::mutex> guard(mutex);
    out = 0;
    if (unavailable) return FloorRead::Unavailable;
    const auto it = values.find({static_cast<unsigned>(kind), id});
    if (it == values.end()) return FloorRead::NotEstablished;
    out = it->second;
    return FloorRead::Present;
  }
  FloorAdvance advance(Kind kind, const char* id, uint32_t proposed) {
    std::lock_guard<std::mutex> guard(mutex);
    if (unavailable || proposed == failSecurityVersion) return FloorAdvance::Unavailable;
    auto& current = values[{static_cast<unsigned>(kind), id}];
    if (proposed < current) return FloorAdvance::Downgrade;
    if (proposed == current) return FloorAdvance::AlreadyAtOrAbove;
    current = proposed;
    return FloorAdvance::Advanced;
  }
};

struct Generation {
  PackageArchive archive{};
  std::array<uint8_t, 32> digest{};
  bool authenticated = true;
  bool partialCleanup = false;
};
struct Filesystem {
  std::map<std::string, Generation> dirs;
  std::string failFrom, failTo;
  bool corruptAfterPublish = false;
  unsigned purges = 0;
  bool failPurgeOnce = false;
  bool exists(const char* path) { return dirs.count(path) != 0; }
  bool rename(const char* from, const char* to) {
    if ((failFrom == from && failTo == to) || !exists(from) || exists(to)) return false;
    auto moved = dirs.extract(from);
    moved.key() = to;
    dirs.insert(std::move(moved));
    if (corruptAfterPublish && std::strcmp(from, kSignedExtractStage) == 0) {
      dirs.at(to).authenticated = false;
      corruptAfterPublish = false;
    }
    return true;
  }
  bool verify(const char* path, const uint8_t* expected, PackageArchive& out) {
    out = {};
    const auto it = dirs.find(path);
    if (it == dirs.end() || !it->second.authenticated || it->second.partialCleanup)
      return false;
    if (expected && std::memcmp(expected, it->second.digest.data(), 32)) return false;
    out = it->second.archive;
    return true;
  }
  bool purge(const char* path) {
    ++purges;
    if (!exists(path)) return false;
    if (failPurgeOnce) {
      failPurgeOnce = false;
      dirs.at(path).partialCleanup = true;
      return false;
    }
    dirs.erase(path); // Simulates selective provenance-last cleanup.
    return true;
  }
};

PackageArchive pkg(Kind kind, const char* id, const char* version, uint32_t security) {
  PackageArchive out{};
  assert(makeIdentity(kind, id, version, "driver.elf", false, &out.identity));
  out.securityVersion = security;
  out.keyId = 7;
  return out;
}
std::array<uint8_t, 32> digest(uint8_t value) {
  std::array<uint8_t, 32> out{};
  out.fill(value);
  return out;
}
struct Harness {
  Store store;
  Filesystem fs;
  PackageArchive observed{};
  SignedTransactionPaths paths{};
  Harness(Kind kind = Kind::Driver, const char* id = "gps-nmea") {
    assert(signedTransactionPaths(kind, id, paths));
  }
  void stage(const PackageArchive& archive, uint8_t fingerprint) {
    assert(!fs.exists(kSignedExtractStage));
    fs.dirs[kSignedExtractStage] = {archive, digest(fingerprint), true, false};
  }
  SignedTransactionResult publish(const PackageArchive& candidate,
                                  uint8_t fingerprint, bool first = false,
                                  bool semverDowngrade = false) {
    const auto expected = digest(fingerprint);
    return publishSignedDirectoryTransaction(fs, candidate, expected.data(),
        [this](const char* path, const uint8_t* expected, PackageArchive& out) {
          return fs.verify(path, expected, out);
        },
        [this](const char* path) { return fs.purge(path); },
        store, observed, first, semverDowngrade);
  }
  SignedTransactionResult recover(bool first = false) {
    const Kind kind = Kind::Driver;
    return recoverSignedDirectoryTransaction(fs, kind, "gps-nmea",
        [this](const char* path, const uint8_t* expected, PackageArchive& out) {
          return fs.verify(path, expected, out);
        },
        [this](const char* path) { return fs.purge(path); },
        store, observed, first);
  }
  uint32_t floor() { uint32_t result = 0; assert(store.read(Kind::Driver, "gps-nmea", result) == FloorRead::Present); return result; }
};

void firstAndUpgrade() {
  Harness h;
  const auto first = pkg(Kind::Driver, "gps-nmea", "1.0.0", 3);
  h.stage(first, 1);
  assert(h.publish(first, 1) == SignedTransactionResult::StageRejected);
  assert(h.publish(first, 1, true) == SignedTransactionResult::Published);
  assert(h.floor() == 3 && h.fs.exists(h.paths.target) &&
         !h.fs.exists(h.paths.backup));
  assert(h.recover() == SignedTransactionResult::VerifiedGeneration);
  h.stage(first, 2);
  assert(h.publish(first, 2) == SignedTransactionResult::VersionRejected);
  h.fs.dirs.erase(kSignedExtractStage);
  const auto newer = pkg(Kind::Driver, "gps-nmea", "1.0.1", 5);
  h.stage(newer, 3);
  assert(h.publish(newer, 9) == SignedTransactionResult::StageRejected);
  assert(h.publish(newer, 3) == SignedTransactionResult::Published);
  assert(h.floor() == 5 && !h.fs.exists(h.paths.backup));
  h.stage(pkg(Kind::Driver, "gps-nmea", "1.0.2", 4), 4);
  assert(h.publish(pkg(Kind::Driver, "gps-nmea", "1.0.2", 4), 4) ==
         SignedTransactionResult::StageRejected);
  h.fs.dirs.erase(kSignedExtractStage);
  h.stage(pkg(Kind::Driver, "gps-nmea", "1.0.0", 5), 5);
  assert(h.publish(pkg(Kind::Driver, "gps-nmea", "1.0.0", 5), 5) ==
         SignedTransactionResult::VersionRejected);
}
void activeLeaseAndRenameFailure() {
  Harness h;
  auto old = pkg(Kind::Driver, "gps-nmea", "1.0.0", 2);
  auto next = pkg(Kind::Driver, "gps-nmea", "1.0.1", 4);
  h.fs.dirs[h.paths.target] = {old, digest(1), true, false};
  assert(h.store.advance(Kind::Driver, "gps-nmea", 2) == FloorAdvance::Advanced);
  h.stage(next, 2);
  assert(systemPackageUseGate().pin(h.paths.target));
  assert(h.publish(next, 2) == SignedTransactionResult::InUse);
  assert(systemPackageUseGate().unpin(h.paths.target));
  h.fs.failFrom = kSignedExtractStage;
  h.fs.failTo = h.paths.target;
  assert(h.publish(next, 2) == SignedTransactionResult::RenameFailed);
  assert(h.floor() == 2 && h.fs.exists(h.paths.target) &&
         h.fs.exists(kSignedExtractStage) && !h.fs.exists(h.paths.backup));
  h.fs.failFrom.clear(); h.fs.failTo.clear();
  assert(h.publish(next, 2) == SignedTransactionResult::Published);
  assert(h.floor() == 4);
}
void recoveryAcrossCuts() {
  Harness h;
  const auto old = pkg(Kind::Driver, "gps-nmea", "1.0.0", 3);
  const auto next = pkg(Kind::Driver, "gps-nmea", "1.0.1", 5);
  h.fs.dirs[h.paths.target] = {old, digest(1), true, false};
  assert(h.store.advance(Kind::Driver, "gps-nmea", 3) == FloorAdvance::Advanced);
  h.stage(next, 2);
  // Reset immediately after target -> backup: restore the old, still allowed
  // version without raising NVS. The new stage remains untouched.
  assert(h.fs.rename(h.paths.target, h.paths.backup));
  assert(h.recover() == SignedTransactionResult::RecoveredGeneration);
  assert(h.floor() == 3 && h.fs.exists(kSignedExtractStage));
  // Reset after stage -> target, with an old backup still present.
  assert(h.fs.rename(h.paths.target, h.paths.backup));
  assert(h.fs.rename(kSignedExtractStage, h.paths.target));
  h.fs.failPurgeOnce = true;
  assert(h.recover() == SignedTransactionResult::BackupCleanupPending);
  assert(h.floor() == 3 && h.fs.exists(h.paths.backup));
  assert(h.recover() == SignedTransactionResult::VerifiedGeneration);
  assert(h.floor() == 5 && !h.fs.exists(h.paths.backup));
}
void floorFailureAndInvalidTarget() {
  Harness h;
  auto first = pkg(Kind::Driver, "gps-nmea", "1.0.0", 4);
  h.stage(first, 1);
  h.store.failSecurityVersion = 4;
  assert(h.publish(first, 1, true) == SignedTransactionResult::FloorCommitPending);
  assert(h.fs.exists(h.paths.target) && !h.fs.exists(h.paths.backup));
  uint32_t observed = 0;
  assert(h.store.read(Kind::Driver, "gps-nmea", observed) == FloorRead::NotEstablished);
  h.store.failSecurityVersion = 0;
  assert(h.recover(true) == SignedTransactionResult::VerifiedGeneration);
  assert(h.floor() == 4);
  auto next = pkg(Kind::Driver, "gps-nmea", "1.0.1", 5);
  h.stage(next, 2);
  h.fs.corruptAfterPublish = true;
  assert(h.publish(next, 2) == SignedTransactionResult::PostPublishRejected);
  assert(h.floor() == 4 && h.fs.exists(h.paths.target) &&
         h.fs.exists(kSignedExtractStage));
  h.fs.dirs.at(kSignedExtractStage).authenticated = true;
  assert(h.publish(next, 2) == SignedTransactionResult::Published);
  assert(h.floor() == 5);
}
void failClosedRecovery() {
  Harness h;
  assert(h.recover() == SignedTransactionResult::NoInstalledGeneration);
  const auto old = pkg(Kind::Driver, "gps-nmea", "1.0.0", 2);
  const auto next = pkg(Kind::Driver, "gps-nmea", "1.0.1", 4);
  h.fs.dirs[h.paths.backup] = {old, digest(1), true, false};
  assert(h.store.advance(Kind::Driver, "gps-nmea", 2) == FloorAdvance::Advanced);
  h.fs.dirs[h.paths.target] = {next, digest(2), false, false};
  assert(h.recover() == SignedTransactionResult::TargetRejected);
  assert(h.fs.exists(h.paths.target) && h.fs.exists(h.paths.backup));
  h.fs.dirs.erase(h.paths.target);
  h.fs.dirs.at(h.paths.backup).authenticated = false;
  assert(h.recover() == SignedTransactionResult::BackupRejected);
  assert(!h.fs.exists(h.paths.target));
}
} // namespace

int main() {
  firstAndUpgrade();
  activeLeaseAndRenameFailure();
  recoveryAcrossCuts();
  floorFailureAndInvalidTarget();
  failClosedRecovery();
  SignedTransactionPaths invalid{};
  assert(!signedTransactionPaths(Kind::Driver, "../driver", invalid));
  assert(signedTransactionPaths(Kind::Application, "reader", invalid));
  assert(std::strcmp(invalid.target, "/Apps/reader") == 0);
  assert(signedTransactionPaths(Kind::Service, "logger", invalid));
  assert(signedTransactionPaths(Kind::Provider, "clock", invalid));
  std::puts("Signed publication: four-kind paths, gated rename, floor commit, staged recovery and power-cut faults passed");
  return 0;
}
