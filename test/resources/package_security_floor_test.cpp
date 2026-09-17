#include "runtime/packages/PackageSecurityFloor.h"

#include <cassert>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <utility>

using namespace RuntimePackages;
namespace {
struct DurableRecords {
  std::map<std::pair<unsigned, std::string>, uint32_t> records;
  bool accessible = true;
  bool failCommit = false;
  std::mutex mutex;
  FloorRead read(Kind kind, const char* id, uint32_t& floor) {
    std::lock_guard<std::mutex> lock(mutex);
    floor = 0;
    if (!accessible) return FloorRead::Unavailable;
    auto it = records.find({static_cast<unsigned>(kind), id});
    if (it == records.end()) return FloorRead::NotEstablished;
    floor = it->second;
    return FloorRead::Present;
  }
  FloorAdvance advance(Kind kind, const char* id, uint32_t proposed) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!accessible || failCommit) return FloorAdvance::Unavailable;
    auto& current = records[{static_cast<unsigned>(kind), id}];
    if (proposed < current) return FloorAdvance::Downgrade;
    if (proposed == current) return FloorAdvance::AlreadyAtOrAbove;
    current = proposed;
    return FloorAdvance::Advanced;
  }
};
PackageArchive package(Kind kind, const char* id, const char* version, uint32_t security) {
  PackageArchive archive{};
  const bool valid = makeIdentity(kind, id, version, "module.elf", false, &archive.identity);
  assert(valid);
  archive.securityVersion = security;
  return archive;
}
void firstInstallAndRestart() {
  DurableRecords persisted;
  auto gps = package(Kind::Driver, "gps-nmea", "1.0.0", 4);
  assert(checkPackageSecurityFloor(persisted, gps, false) == FloorCheck::Unavailable);
  assert(checkPackageSecurityFloor(persisted, gps, true) == FloorCheck::Allowed);
  // A failed write must not be mistaken for a committed policy change.
  persisted.failCommit = true;
  assert(advancePackageSecurityFloor(persisted, gps) == FloorAdvance::Unavailable);
  assert(checkPackageSecurityFloor(persisted, gps, false) == FloorCheck::Unavailable);
  persisted.failCommit = false;
  assert(advancePackageSecurityFloor(persisted, gps) == FloorAdvance::Advanced);
  assert(advancePackageSecurityFloor(persisted, gps) == FloorAdvance::AlreadyAtOrAbove);
  // An independent restart-facing policy object sees the durable state.
  auto downgraded = package(Kind::Driver, "gps-nmea", "9.9.9", 3);
  assert(checkPackageSecurityFloor(persisted, downgraded, true) == FloorCheck::SecurityRollback);
  assert(advancePackageSecurityFloor(persisted, downgraded) == FloorAdvance::Downgrade);
  assert(checkPackageSecurityFloor(persisted, gps, false) == FloorCheck::Allowed);
  auto upgrade = package(Kind::Driver, "gps-nmea", "1.0.1", 6);
  assert(checkPackageSecurityFloor(persisted, upgrade, false) == FloorCheck::Allowed);
  assert(advancePackageSecurityFloor(persisted, upgrade) == FloorAdvance::Advanced);
  assert(checkPackageSecurityFloor(persisted, gps, false) == FloorCheck::SecurityRollback);
  assert(persisted.records.at({static_cast<unsigned>(Kind::Driver), "gps-nmea"}) == 6);
}
void distinctKindAndId() {
  DurableRecords store;
  auto driver = package(Kind::Driver, "shared-id", "1.0.0", 7);
  auto app = package(Kind::Application, "shared-id", "1.0.0", 2);
  auto service = package(Kind::Service, "shared-id", "1.0.0", 1);
  auto provider = package(Kind::Provider, "shared-id", "1.0.0", 3);
  assert(advancePackageSecurityFloor(store, driver) == FloorAdvance::Advanced);
  assert(advancePackageSecurityFloor(store, app) == FloorAdvance::Advanced);
  assert(advancePackageSecurityFloor(store, service) == FloorAdvance::Advanced);
  assert(advancePackageSecurityFloor(store, provider) == FloorAdvance::Advanced);
  assert(checkPackageSecurityFloor(store, app, false) == FloorCheck::Allowed);
  auto other = package(Kind::Driver, "different-id", "1.0.0", 1);
  assert(checkPackageSecurityFloor(store, other, false) == FloorCheck::Unavailable);
  store.accessible = false;
  assert(checkPackageSecurityFloor(store, driver, true) == FloorCheck::Unavailable);
  assert(advancePackageSecurityFloor(store, app) == FloorAdvance::Unavailable);
}
void invalidRecords() {
  DurableRecords store;
  auto candidate = package(Kind::Application, "reader", "1.0.0", 1);
  candidate.securityVersion = 0;
  assert(checkPackageSecurityFloor(store, candidate, true) == FloorCheck::InvalidIdentity);
  assert(advancePackageSecurityFloor(store, candidate) == FloorAdvance::Unavailable);
  candidate.securityVersion = 1;
  candidate.identity.kind = static_cast<Kind>(99);
  assert(checkPackageSecurityFloor(store, candidate, true) == FloorCheck::InvalidIdentity);
  candidate = package(Kind::Application, "reader", "1.0.0", 1);
  candidate.identity.legacyVersion = true;
  assert(checkPackageSecurityFloor(store, candidate, true) == FloorCheck::InvalidIdentity);
  candidate.identity.legacyVersion = false;
  store.records[{static_cast<unsigned>(Kind::Application), "reader"}] = 0;
  assert(checkPackageSecurityFloor(store, candidate, true) == FloorCheck::Unavailable);
}
} // namespace
int main() {
  firstInstallAndRestart();
  distinctKindAndId();
  invalidRecords();
  std::puts("Package security floors: monotonic per-kind/ID, restart, commit-failure and fail-closed policy passed");
}
