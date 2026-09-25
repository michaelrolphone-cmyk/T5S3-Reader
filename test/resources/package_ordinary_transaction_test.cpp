#include "runtime/packages/PackageOrdinaryTransaction.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>

using namespace RuntimePackages;
namespace {
struct Generation {
  Identity identity{};
  bool valid = true;
  bool partiallyPurged = false;
};
struct Storage {
  std::map<std::string, Generation> dirs;
  std::string failSource;
  std::string failDestination;
  bool failCleanupOnce = false;
  bool corruptOnPublish = false;
  bool exists(const char* path) { return dirs.count(path) != 0; }
  bool rename(const char* from, const char* to) {
    if (!exists(from) || exists(to) ||
        (failSource == from && failDestination == to)) return false;
    auto node = dirs.extract(from);
    node.key() = to;
    dirs.insert(std::move(node));
    if (corruptOnPublish && std::string(from).find("pkg-stage") != std::string::npos) {
      dirs.at(to).valid = false;
      corruptOnPublish = false;
    }
    return true;
  }
  bool verify(const char* path, Identity& out) {
    out = {};
    auto it = dirs.find(path);
    if (it == dirs.end() || !it->second.valid || it->second.partiallyPurged)
      return false;
    out = it->second.identity;
    return true;
  }
  bool purge(const char* path) {
    if (!exists(path)) return false;
    if (failCleanupOnce) {
      failCleanupOnce = false;
      dirs.at(path).partiallyPurged = true;
      return false;
    }
    dirs.erase(path); // Simulates selective known-entry, manifest-last purge.
    return true;
  }
};
Identity identity(Kind kind, const char* id, const char* version) {
  Identity out{};
  assert(makeIdentity(kind, id, version, "module.elf", false, &out));
  return out;
}
struct Harness {
  Storage storage;
  OrdinaryTransactionPaths paths{};
  Identity observed{};
  Kind kind;
  const char* id;
  Harness(Kind kindValue, const char* packageId)
      : kind(kindValue), id(packageId) {
    assert(ordinaryTransactionPaths(kind, id, paths));
  }
  void stage(const char* version) {
    assert(!storage.exists(paths.stage));
    storage.dirs[paths.stage] = {identity(kind, id, version), true, false};
  }
  OrdinaryTransactionResult install(const char* version, bool allowDowngrade = false) {
    return publishOrdinaryPackage(storage, identity(kind, id, version),
        [this](const char* path, Identity& result) {
          return storage.verify(path, result);
        },
        [this](const char* path) { return storage.purge(path); }, observed,
        allowDowngrade);
  }
  OrdinaryTransactionResult recover() {
    return recoverOrdinaryPackage(storage, kind, id,
        [this](const char* path, Identity& result) {
          return storage.verify(path, result);
        },
        [this](const char* path) { return storage.purge(path); }, observed);
  }
  OrdinaryTransactionResult uninstall() {
    return uninstallOrdinaryPackage(storage, kind, id,
        [this](const char* path, Identity& result) {
          return storage.verify(path, result);
        },
        [this](const char* path) { return storage.purge(path); }, observed);
  }
};

void lifecycle(Kind kind) {
  Harness h(kind, "simple-module");
  assert(h.recover() == OrdinaryTransactionResult::NoInstalledPackage);
  h.stage("1.0.0");
  assert(h.install("1.0.0") == OrdinaryTransactionResult::Published);
  assert(h.recover() == OrdinaryTransactionResult::InstalledVerified);
  h.stage("1.0.1");
  assert(h.install("1.0.1") == OrdinaryTransactionResult::Published);
  assert(h.recover() == OrdinaryTransactionResult::InstalledVerified);
  h.stage("1.0.0");
  assert(h.install("1.0.0") == OrdinaryTransactionResult::VersionRejected);
  assert(h.storage.exists(h.paths.target) && h.storage.exists(h.paths.stage));
  h.storage.dirs.erase(h.paths.stage);
  assert(systemPackageUseGate().pin(h.paths.target));
  assert(h.uninstall() == OrdinaryTransactionResult::InUse);
  assert(systemPackageUseGate().unpin(h.paths.target));
  assert(h.uninstall() == OrdinaryTransactionResult::Removed);
  assert(h.recover() == OrdinaryTransactionResult::NoInstalledPackage);
  assert(!h.storage.exists(h.paths.target));
}

void explicitDowngradePolicy() {
  Harness h(Kind::Application, "downgrade-app");
  h.stage("2.0.0");
  assert(h.install("2.0.0") == OrdinaryTransactionResult::Published);
  h.stage("1.5.0");
  assert(h.install("1.5.0") == OrdinaryTransactionResult::VersionRejected);
  assert(h.storage.exists(h.paths.stage));
  h.storage.dirs.erase(h.paths.stage);
  h.stage("1.5.0");
  assert(h.install("1.5.0", true) == OrdinaryTransactionResult::Published);
  assert(std::strcmp(h.observed.version, "1.5.0") == 0);
  h.stage("1.5.0");
  assert(h.install("1.5.0", true) == OrdinaryTransactionResult::VersionRejected);
  assert(h.storage.exists(h.paths.target) && h.storage.exists(h.paths.stage));
  h.storage.dirs.erase(h.paths.stage);
}

void powerCutsAndFailures() {
  Harness h(Kind::Driver, "gps-nmea");
  h.stage("1.0.0");
  assert(h.install("1.0.0") == OrdinaryTransactionResult::Published);
  h.stage("1.0.1");
  h.storage.failSource = h.paths.stage;
  h.storage.failDestination = h.paths.target;
  assert(h.install("1.0.1") == OrdinaryTransactionResult::RenameFailed);
  assert(h.storage.exists(h.paths.target) && !h.storage.exists(h.paths.backup));
  h.storage.failSource.clear(); h.storage.failDestination.clear();
  // Power cut after old target->backup but before new stage->target.
  assert(h.storage.rename(h.paths.target, h.paths.backup));
  assert(h.recover() == OrdinaryTransactionResult::PreviousRestored);
  assert(h.storage.exists(h.paths.stage));
  // Power cut after stage->target, before previous-generation cleanup.
  assert(h.storage.rename(h.paths.target, h.paths.backup));
  assert(h.storage.rename(h.paths.stage, h.paths.target));
  h.storage.failCleanupOnce = true;
  assert(h.recover() == OrdinaryTransactionResult::CleanupPending);
  assert(h.recover() == OrdinaryTransactionResult::InstalledVerified);
  assert(!h.storage.exists(h.paths.backup));
  assert(std::strcmp(h.observed.version, "1.0.1") == 0);
  // Interrupted uninstall never resurrects removed executable.
  h.storage.failCleanupOnce = true;
  assert(h.uninstall() == OrdinaryTransactionResult::RemovalPending);
  assert(!h.storage.exists(h.paths.target) && h.storage.exists(h.paths.removing));
  assert(h.recover() == OrdinaryTransactionResult::Removed);
  assert(h.recover() == OrdinaryTransactionResult::NoInstalledPackage);
  assert(!h.storage.exists(h.paths.removing));
}

void invalidAndPinned() {
  Harness h(Kind::Service, "worker");
  h.stage("1.0.0");
  assert(systemPackageUseGate().pin(h.paths.target));
  assert(h.install("1.0.0") == OrdinaryTransactionResult::InUse);
  assert(systemPackageUseGate().unpin(h.paths.target));
  h.storage.dirs.at(h.paths.stage).valid = false;
  assert(h.install("1.0.0") == OrdinaryTransactionResult::InvalidStage);
  h.storage.dirs.at(h.paths.stage).valid = true;
  assert(h.install("1.0.0") == OrdinaryTransactionResult::Published);
  h.storage.dirs.at(h.paths.target).valid = false;
  assert(h.recover() == OrdinaryTransactionResult::InvalidInstalled);
  assert(h.uninstall() == OrdinaryTransactionResult::InvalidInstalled);
  assert(h.storage.exists(h.paths.target));
  OrdinaryTransactionPaths paths{};
  assert(!ordinaryTransactionPaths(Kind::Driver, "../escape", paths));
}
} // namespace

int main() {
  lifecycle(Kind::Application);
  lifecycle(Kind::Driver);
  lifecycle(Kind::Service);
  lifecycle(Kind::Provider);
  explicitDowngradePolicy();
  powerCutsAndFailures();
  invalidAndPinned();
  std::puts("Ordinary lifecycle: four-kind install/update/inventory/uninstall, mapping gate and power-cut recovery passed");
}
