#include "runtime/packages/PackageOrdinaryTransaction.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

using namespace RuntimePackages;
namespace {
struct Directory {
  Identity identity{};
  bool verified = true;
};
struct Fs {
  std::map<std::string, Directory> files;
  bool refusePurge = false;
  int renameNumber = 0;
  int failRename = -1;
  bool exists(const char* path) const { return files.count(path) != 0; }
  bool rename(const char* from, const char* to) {
    if (++renameNumber == failRename || !exists(from) || exists(to)) return false;
    files[to] = files.at(from);
    files.erase(from);
    return true;
  }
  bool verify(const char* path, Identity& out) const {
    out = {};
    const auto it = files.find(path);
    if (it == files.end() || !it->second.verified) return false;
    out = it->second.identity;
    return true;
  }
  bool purge(const char* path) {
    if (refusePurge || !exists(path)) return false;
    return files.erase(path) == 1;
  }
};

Identity identity(const char* version) {
  Identity result{};
  assert(makeIdentity(Kind::Driver, "gps-nmea", version, "driver.elf", false, &result));
  return result;
}
OrdinaryTransactionPaths paths() {
  OrdinaryTransactionPaths result{};
  assert(ordinaryTransactionPaths(Kind::Driver, "gps-nmea", result));
  assert(std::strcmp(result.target, "/Drivers/gps-nmea") == 0);
  assert(std::strcmp(result.stage, "/Drivers/.gps-nmea.pkg-stage") == 0);
  assert(std::strcmp(result.backup, "/Drivers/.gps-nmea.pkg-previous") == 0);
  return result;
}
template <typename Function>
OrdinaryTransactionResult perform(Fs& disk, Function operation) {
  Identity observed{};
  const auto verify = [&disk](const char* path, Identity& out) {
    return disk.verify(path, out);
  };
  const auto purge = [&disk](const char* path) { return disk.purge(path); };
  return operation(verify, purge, observed);
}
OrdinaryTransactionResult publish(Fs& disk, const Identity& candidate) {
  return perform(disk, [&](auto verify, auto purge, Identity& observed) {
    return publishOrdinaryPackage(disk, candidate, verify, purge, observed);
  });
}
OrdinaryTransactionResult recover(Fs& disk) {
  return perform(disk, [&](auto verify, auto purge, Identity& observed) {
    return recoverOrdinaryPackage(disk, Kind::Driver, "gps-nmea", verify, purge, observed);
  });
}
OrdinaryTransactionResult uninstall(Fs& disk) {
  return perform(disk, [&](auto verify, auto purge, Identity& observed) {
    return uninstallOrdinaryPackage(disk, Kind::Driver, "gps-nmea", verify, purge, observed);
  });
}

void updateAndRejectDowngrade() {
  Fs disk;
  const auto p = paths();
  disk.files[p.target] = {identity("1.0.0"), true};
  disk.files[p.stage] = {identity("1.0.1"), true};
  assert(publish(disk, identity("1.0.1")) == OrdinaryTransactionResult::Published);
  assert(disk.exists(p.target) && !disk.exists(p.backup) && !disk.exists(p.stage));
  assert(std::strcmp(disk.files[p.target].identity.version, "1.0.1") == 0);
  disk.files[p.stage] = {identity("1.0.1"), true};
  assert(publish(disk, identity("1.0.1")) == OrdinaryTransactionResult::VersionRejected);
  assert(disk.exists(p.stage)); // A failed install cannot consume its staged generation.
  disk.files[p.stage] = {identity("0.9.9"), true};
  assert(publish(disk, identity("0.9.9")) == OrdinaryTransactionResult::VersionRejected);
  assert(std::strcmp(disk.files[p.target].identity.version, "1.0.1") == 0);
}

void mappedDriverAndRenameFailure() {
  Fs disk;
  const auto p = paths();
  disk.files[p.target] = {identity("1.0.0"), true};
  disk.files[p.stage] = {identity("1.0.1"), true};
  assert(systemPackageUseGate().pin(p.target));
  assert(publish(disk, identity("1.0.1")) == OrdinaryTransactionResult::InUse);
  assert(disk.exists(p.target) && disk.exists(p.stage) && !disk.exists(p.backup));
  assert(systemPackageUseGate().unpin(p.target));
  disk.failRename = 2;
  assert(publish(disk, identity("1.0.1")) == OrdinaryTransactionResult::RenameFailed);
  assert(disk.exists(p.target) && disk.exists(p.stage));
  assert(std::strcmp(disk.files[p.target].identity.version, "1.0.0") == 0);
  disk.failRename = -1;
  assert(publish(disk, identity("1.0.1")) == OrdinaryTransactionResult::Published);
}

void recoveryAndUninstall() {
  Fs disk;
  const auto p = paths();
  disk.files[p.backup] = {identity("1.0.0"), true};
  assert(recover(disk) == OrdinaryTransactionResult::PreviousRestored);
  assert(disk.exists(p.target) && !disk.exists(p.backup));
  disk.files[p.stage] = {identity("2.0.0"), false};
  assert(publish(disk, identity("2.0.0")) == OrdinaryTransactionResult::InvalidStage);
  assert(disk.exists(p.target));
  disk.files.erase(p.stage);
  disk.refusePurge = true;
  assert(uninstall(disk) == OrdinaryTransactionResult::RemovalPending);
  assert(!disk.exists(p.target) && disk.exists(p.removing));
  disk.refusePurge = false;
  assert(recover(disk) == OrdinaryTransactionResult::Removed);
  assert(!disk.exists(p.target) && !disk.exists(p.removing));
}
} // namespace

int main() {
  updateAndRejectDowngrade();
  mappedDriverAndRenameFailure();
  recoveryAndUninstall();
  std::puts("Driver transaction adapter: upgrade, stage retention, in-use, rollback, recovery and uninstall PASS");
}
