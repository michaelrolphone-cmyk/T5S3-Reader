#include "runtime/packages/PackageTransaction.h"

#include <cassert>
#include <cstdio>
#include <map>
#include <string>

using RuntimePackages::TransactionPaths;
using RuntimePackages::publishDirectoryTransaction;
using RuntimePackages::recoverDirectoryTransaction;

namespace {
constexpr TransactionPaths kPaths{"/Drivers/gps", "/Drivers/.gps.install", "/Drivers/.gps.previous"};

struct FakeStorage {
  // True means a fully verified package, false means corrupt or unmanaged.
  std::map<std::string, bool> entries;
  int renameCount = 0;
  int failRename = -1;
  bool failPurge = false;
  bool failPublishedVerification = false;

  bool exists(const char* name) const { return entries.count(name) != 0; }
  bool rename(const char* source, const char* destination) {
    ++renameCount;
    if (renameCount == failRename || !exists(source) || exists(destination)) return false;
    entries[destination] = entries.at(source);
    entries.erase(source);
    return true;
  }
  bool verify(const char* path) const {
    if (failPublishedVerification && std::string(path) == kPaths.target &&
        renameCount >= 2) return false;
    const auto found = entries.find(path);
    return found != entries.end() && found->second;
  }
  bool purge(const char* path) {
    if (failPurge || !verify(path)) return false;
    return entries.erase(path) == 1;
  }
  bool recover() {
    return recoverDirectoryTransaction(*this, kPaths,
       [this](const char* path) { return verify(path); },
       [this](const char* path) { return purge(path); });
  }
  bool publish(bool replaceAllowed = true) {
    return publishDirectoryTransaction(*this, kPaths,
       [this](const char* path) { return verify(path); },
       [this](const char* path) { return purge(path); }, replaceAllowed);
  }
};

void freshInstall() {
  FakeStorage disk;
  disk.entries[kPaths.stage] = true;
  assert(disk.publish());
  assert(disk.exists(kPaths.target) && !disk.exists(kPaths.stage));
  assert(!disk.exists(kPaths.backup));
  assert(disk.recover());
}

void upgrade() {
  FakeStorage disk;
  disk.entries[kPaths.target] = true;
  disk.entries[kPaths.stage] = true;
  assert(disk.publish());
  assert(disk.exists(kPaths.target) && !disk.exists(kPaths.backup));
  assert(!disk.exists(kPaths.stage));
}

void renameFailures() {
  // Rename 1 backs up the current package, rename 2 publishes the stage,
  // rename 3 attempts restoration after a publish failure.
  for (int failure = 1; failure <= 3; ++failure) {
    FakeStorage disk;
    disk.entries[kPaths.target] = true;
    disk.entries[kPaths.stage] = true;
    disk.failRename = failure;
    const bool published = disk.publish();
    if (failure == 3) {
      // Successful publish does not need a restoration rename.
      assert(published);
      continue;
    }
    assert(!published);
    assert(disk.exists(kPaths.target) || disk.exists(kPaths.backup));
    disk.failRename = -1; // A reboot clears transient I/O failures.
    assert(disk.recover());
    assert(disk.exists(kPaths.target) && disk.verify(kPaths.target));
  }
  // If both commit and immediate rollback fail, recovery still retains old.
  FakeStorage disk;
  disk.entries[kPaths.backup] = true;
  disk.entries[kPaths.stage] = true;
  disk.failRename = 1;
  assert(!disk.recover());
  assert(disk.exists(kPaths.backup) && disk.verify(kPaths.backup));
  disk.failRename = -1;
  assert(disk.recover());
}

void powerCuts() {
  FakeStorage beforePublish;
  beforePublish.entries[kPaths.backup] = true; // power cut after target -> backup
  beforePublish.entries[kPaths.stage] = true;
  assert(beforePublish.recover());
  assert(beforePublish.exists(kPaths.target));
  assert(!beforePublish.exists(kPaths.backup));
  assert(beforePublish.publish());

  FakeStorage afterPublish;
  afterPublish.entries[kPaths.target] = true; // power cut after stage -> target
  afterPublish.entries[kPaths.backup] = true;
  assert(afterPublish.recover());
  assert(afterPublish.exists(kPaths.target) && !afterPublish.exists(kPaths.backup));
}

void refuseUnsafePaths() {
  FakeStorage busy;
  busy.entries[kPaths.target] = true;
  busy.entries[kPaths.stage] = true;
  assert(!busy.publish(false));
  assert(busy.renameCount == 0);

  FakeStorage corruptStage;
  corruptStage.entries[kPaths.target] = true;
  corruptStage.entries[kPaths.stage] = false;
  assert(!corruptStage.publish());
  assert(corruptStage.entries.at(kPaths.target));

  FakeStorage unknownTarget;
  unknownTarget.entries[kPaths.target] = false;
  unknownTarget.entries[kPaths.stage] = true;
  assert(!unknownTarget.publish());
  assert(unknownTarget.renameCount == 0);

  FakeStorage badBackup;
  badBackup.entries[kPaths.backup] = false;
  badBackup.entries[kPaths.stage] = true;
  assert(!badBackup.publish());
  assert(badBackup.exists(kPaths.backup));

  FakeStorage corruptPublished;
  corruptPublished.entries[kPaths.target] = false;
  corruptPublished.entries[kPaths.backup] = true;
  assert(!corruptPublished.recover());
  assert(corruptPublished.exists(kPaths.backup));

  assert(!recoverDirectoryTransaction(busy,
      TransactionPaths{nullptr, kPaths.stage, kPaths.backup},
      [&busy](const char* path) { return busy.verify(path); },
      [&busy](const char* path) { return busy.purge(path); }));
}

void cleanupFailurePreservesBackup() {
  FakeStorage disk;
  disk.entries[kPaths.target] = true;
  disk.entries[kPaths.stage] = true;
  disk.failPurge = true;
  assert(disk.publish());
  assert(disk.exists(kPaths.target) && disk.exists(kPaths.backup));
  assert(!disk.recover());
  assert(disk.exists(kPaths.backup));
  disk.failPurge = false;
  assert(disk.recover());
  assert(!disk.exists(kPaths.backup));
}

void postPublishValidationFailure() {
  FakeStorage disk;
  disk.entries[kPaths.target] = true;
  disk.entries[kPaths.stage] = true;
  disk.failPublishedVerification = true;
  assert(!disk.publish());
  assert(disk.exists(kPaths.target) || disk.exists(kPaths.backup));
  disk.failPublishedVerification = false;
  assert(disk.recover());
  assert(disk.exists(kPaths.target) && disk.verify(kPaths.target));
}
} // namespace

int main() {
  freshInstall();
  upgrade();
  renameFailures();
  powerCuts();
  refuseUnsafePaths();
  cleanupFailurePreservesBackup();
  postPublishValidationFailure();
  std::puts("Package directory transaction: publish, rollback, power-cut recovery and refusal cases passed");
}
