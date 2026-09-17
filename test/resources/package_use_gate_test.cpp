#include "runtime/packages/PackageTransaction.h"

#include <cassert>
#include <cstdio>
#include <map>
#include <string>

using namespace RuntimePackages;

namespace {
constexpr TransactionPaths paths{"/Drivers/gps-nmea", "/Drivers/.gps-nmea.install",
                                 "/Drivers/.gps-nmea.previous"};

struct Disk {
  std::map<std::string, bool> entries;
  unsigned renames = 0;
  bool inspectLoaderDuringVerification = false;
  bool exists(const char* name) const { return entries.count(name) != 0; }
  bool rename(const char* from, const char* to) {
    ++renames;
    if (!exists(from) || exists(to)) return false;
    entries[to] = entries.at(from);
    entries.erase(from);
    return true;
  }
  bool verify(const char* name) const {
    if (inspectLoaderDuringVerification) {
      // A concurrent loader must not acquire its pin in the middle of the
      // transaction, even before the first actual rename.
      assert(!systemPackageUseGate().pin(paths.target));
    }
    auto it = entries.find(name);
    return it != entries.end() && it->second;
  }
  bool purge(const char* name) { return entries.erase(name) == 1; }
  bool publish() {
    return publishDirectoryTransaction(*this, paths,
        [this](const char* path) { return verify(path); },
        [this](const char* path) { return purge(path); }, true);
  }
  bool recover() {
    return recoverDirectoryTransaction(*this, paths,
        [this](const char* path) { return verify(path); },
        [this](const char* path) { return purge(path); });
  }
};

void pinLifecycle() {
  auto& gate = systemPackageUseGate();
  assert(!gate.pin("/sd/Drivers/gps-nmea/driver.elf"));
  assert(!gate.pin("/Drivers/../gps-nmea"));
  assert(!gate.pin("/Drivers/GPS"));
  assert(!gate.pin("/Drivers/gps-nmea/driver.elf"));
  assert(gate.pin(paths.target));
  assert(gate.pin(paths.target));
  assert(gate.pinned(paths.target));
  assert(!gate.beginReplacement(paths.target));
  assert(gate.unpin(paths.target));
  assert(!gate.beginReplacement(paths.target));
  assert(gate.unpin(paths.target));
  assert(!gate.pinned(paths.target));
  assert(!gate.unpin(paths.target));
  assert(gate.beginReplacement(paths.target));
  assert(!gate.pin(paths.target));
  assert(!gate.beginReplacement(paths.target));
  assert(gate.endReplacement(paths.target));
  assert(!gate.endReplacement(paths.target));
  assert(gate.pin(paths.target));
  assert(gate.pin("/Providers/gps-nmea")); // Kind namespace is independent.
  assert(gate.unpin("/Providers/gps-nmea"));
  assert(gate.unpin(paths.target));
}

void refusesLiveReplacement() {
  auto& gate = systemPackageUseGate();
  Disk disk;
  disk.entries[paths.target] = true;
  disk.entries[paths.stage] = true;
  assert(gate.pin(paths.target));
  assert(!disk.publish());
  assert(disk.renames == 0 && disk.exists(paths.target) && disk.exists(paths.stage));
  assert(gate.unpin(paths.target));
  disk.inspectLoaderDuringVerification = true;
  assert(disk.publish());
  assert(disk.exists(paths.target) && !disk.exists(paths.backup));
  assert(gate.pin(paths.target)); // The reservation has been released.
  assert(gate.unpin(paths.target));
}

void refusesMappedRollback() {
  auto& gate = systemPackageUseGate();
  Disk disk;
  disk.entries[paths.backup] = true; // Power loss after target -> backup.
  assert(gate.pin(paths.target));
  assert(!disk.recover());
  assert(disk.renames == 0 && disk.exists(paths.backup));
  assert(gate.unpin(paths.target));
  disk.inspectLoaderDuringVerification = true;
  assert(disk.recover());
  assert(disk.exists(paths.target) && !disk.exists(paths.backup));
  // Read-only inventory may inspect the mapped package with no backup.
  disk.inspectLoaderDuringVerification = false;
  assert(gate.pin(paths.target));
  assert(disk.recover());
  assert(gate.unpin(paths.target));
}

void boundedRegistry() {
  auto& gate = systemPackageUseGate();
  std::string names[PackageUseGate::kCapacity];
  for (size_t i = 0; i < PackageUseGate::kCapacity; ++i) {
    names[i] = "/Drivers/module-" + std::to_string(i);
    assert(gate.pin(names[i].c_str()));
  }
  assert(!gate.pin("/Drivers/overflow"));
  for (const auto& name : names) assert(gate.unpin(name.c_str()));
  assert(gate.pin("/Drivers/overflow"));
  assert(gate.unpin("/Drivers/overflow"));
}
}  // namespace

int main() {
  pinLifecycle();
  refusesLiveReplacement();
  refusesMappedRollback();
  boundedRegistry();
  std::puts("Package use gate: mapping pins, exclusive rename, recovery and capacity passed");
}
