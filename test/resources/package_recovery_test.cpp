#include "runtime/packages/PackageTransaction.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <set>
#include <string>

using namespace RuntimePackages;

namespace {
constexpr TransactionPaths kPaths{"/Drivers/gps", "/Drivers/.gps.install", "/Drivers/.gps.previous"};
struct Storage {
  std::map<std::string, bool> entries;
  std::set<std::string> partiallyPurged;
  bool interruptedCleanup = false;
  bool exists(const char* path) const { return entries.count(path) != 0; }
  bool rename(const char* from, const char* to) {
    if (!exists(from) || exists(to)) return false;
    entries[to] = entries.at(from);
    entries.erase(from);
    return true;
  }
  bool verify(const char* path) const {
    auto found = entries.find(path);
    return found != entries.end() && found->second;
  }
  bool purge(const char* path) {
    if (!verify(path) && !partiallyPurged.count(path)) return false;
    if (interruptedCleanup) {
      entries[path] = false;
      partiallyPurged.insert(path);
      return false;
    }
    partiallyPurged.erase(path);
    return entries.erase(path) == 1;
  }
  bool recover() {
    return recoverDirectoryTransaction(*this, kPaths,
        [this](const char* path) { return verify(path); },
        [this](const char* path) { return purge(path); });
  }
  bool publish() {
    return publishDirectoryTransaction(*this, kPaths,
        [this](const char* path) { return verify(path); },
        [this](const char* path) { return purge(path); }, true);
  }
};

void interruptedCleanupCanRecover() {
  Storage storage;
  storage.entries[kPaths.target] = true;
  storage.entries[kPaths.stage] = true;
  storage.interruptedCleanup = true;
  assert(storage.publish());
  assert(storage.verify(kPaths.target));
  assert(storage.exists(kPaths.backup) && !storage.verify(kPaths.backup));
  // A reboot clears the transient media failure. Retain the new verified
  // generation rather than rejecting the partially removed prior generation.
  storage.interruptedCleanup = false;
  assert(storage.recover());
  assert(storage.verify(kPaths.target) && !storage.exists(kPaths.backup));
}

void refuseUnknownBackup() {
  Storage storage;
  storage.entries[kPaths.target] = true;
  storage.entries[kPaths.backup] = false; // Not in managed-only purge allowlist.
  assert(!storage.recover());
  assert(storage.verify(kPaths.target) && storage.exists(kPaths.backup));
}

void refuseCorruptTargetWithoutDeletingBackup() {
  Storage storage;
  storage.entries[kPaths.target] = false;
  storage.entries[kPaths.backup] = true;
  assert(!storage.recover());
  assert(storage.exists(kPaths.target) && storage.verify(kPaths.backup));
}
} // namespace

int main() {
  interruptedCleanupCanRecover();
  refuseUnknownBackup();
  refuseCorruptTargetWithoutDeletingBackup();
  std::puts("Package recovery: partial cleanup, unmanaged backup and corrupt target passed");
}
