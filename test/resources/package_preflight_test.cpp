#include "runtime/packages/PackagePreflight.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

using namespace RuntimePackages;

namespace {
constexpr const char* kDigest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

struct Fixture {
  Identity identity{};
  PackageEntry entries[3] = {{"driver.elf", 8192, kDigest, true},
                             {"icon.png", 256, kDigest, false},
                             {"schema.json", 1024, kDigest, false}};
  PackageRequirement needs[2] = {{"kernel.serial", 1}, {"kernel.clock", 2}};
  PackageEnvelopeView package{};
  PackageRuntimePolicy runtime{"xtensa-esp32s3", 3, 2, 16384, 32768};
  int resolveCalls = 0;

  Fixture() {
    assert(makeIdentity(Kind::Driver, "gps-nmea", "1.10.0", "driver.elf", false, &identity));
    package = {identity, "xtensa-esp32s3", 2, 2, entries, 3, needs, 2};
  }
  PreflightResult check() {
    return preflightPackage(package, runtime, [this](const char* capability) -> uint32_t {
      ++resolveCalls;
      if (std::strcmp(capability, "kernel.serial") == 0) return 1;
      if (std::strcmp(capability, "kernel.clock") == 0) return 2;
      return 0;
    });
  }
};

void acceptsFourKinds() {
  for (Kind kind : {Kind::Application, Kind::Driver, Kind::Service, Kind::Provider}) {
    Fixture f;
    f.package.identity.kind = kind;
    assert(f.check() == PreflightResult::ReadyForContentVerification);
    assert(f.resolveCalls == 2);
  }
}

void rejectsIdentityAndCompatibility() {
  Fixture f;
  f.package.identity.legacyVersion = true;
  assert(f.check() == PreflightResult::InvalidIdentity);
  f.package.identity.legacyVersion = false;
  f.package.identity.kind = static_cast<Kind>(99);
  assert(f.check() == PreflightResult::InvalidIdentity);
  f.package.identity.kind = Kind::Driver;
  f.package.architecture = "riscv32";
  assert(f.check() == PreflightResult::UnsupportedArchitecture);
  f.package.architecture = "xtensa-esp32s3";
  f.package.minRuntimeApi = 4;
  assert(f.check() == PreflightResult::IncompatibleRuntime);
  f.package.minRuntimeApi = 2;
  f.package.securityVersion = 1;
  assert(f.check() == PreflightResult::SecurityRollback);
  assert(f.resolveCalls == 0);
}

void rejectsUnsafeEntries() {
  Fixture f;
  f.package.entryCount = 0;
  assert(f.check() == PreflightResult::InvalidEntryList);
  f.package.entryCount = kMaxPackageEntries + 1;
  assert(f.check() == PreflightResult::InvalidEntryList);
  f.package.entryCount = 3;
  f.entries[1].name = "../other";
  assert(f.check() == PreflightResult::InvalidEntry);
  f.entries[1].name = "driver.elf";
  assert(f.check() == PreflightResult::DuplicateEntry);
  f.entries[1].name = "other.elf";
  assert(f.check() == PreflightResult::InvalidEntry);
  f.entries[1].name = "icon.png";
  f.entries[0].name = "rogue.elf";
  assert(f.check() == PreflightResult::InvalidEntry);
  f.entries[0].name = "driver.elf";
  f.entries[0].executable = false;
  assert(f.check() == PreflightResult::InvalidEntry);
  f.entries[0].executable = true;
  f.entries[0].sha256 = "abcd";
  assert(f.check() == PreflightResult::InvalidEntry);
  f.entries[0].sha256 = kDigest;
  f.entries[0].sizeBytes = 0;
  assert(f.check() == PreflightResult::InvalidEntry);
  f.entries[0].sizeBytes = 20000;
  assert(f.check() == PreflightResult::InvalidEntry);
  f.entries[0].sizeBytes = 8192;
  f.runtime.maxTotalBytes = 9000;
  assert(f.check() == PreflightResult::ResourceBudgetExceeded);
  assert(f.resolveCalls == 0);
}

void rejectsInvalidDependenciesBeforeLookup() {
  Fixture f;
  f.needs[1].capability = "kernel.serial";
  assert(f.check() == PreflightResult::DuplicateRequirement);
  assert(f.resolveCalls == 0);
  f.needs[1].capability = "kernel/clock";
  assert(f.check() == PreflightResult::InvalidRequirement);
  f.needs[1].capability = "kernel.clock";
  f.needs[1].minApi = 3;
  assert(f.check() == PreflightResult::UnavailableCapability);
  f.package.requirementCount = kMaxPackageRequirements + 1;
  assert(f.check() == PreflightResult::InvalidRequirement);
}

void versionPolicy() {
  assert(comparePackageVersions("1.10.0", "1.9.99") == VersionOrder::Newer);
  assert(comparePackageVersions("2.0.0", "10.0.0") == VersionOrder::Older);
  assert(comparePackageVersions("01.002.3", "1.2.3") == VersionOrder::Equal);
  assert(comparePackageVersions("4294967296.0.0", "1.0.0") == VersionOrder::Invalid);
  assert(comparePackageVersions("1.0", "1.0.0") == VersionOrder::Invalid);
  Identity candidate{}, installed{};
  assert(makeIdentity(Kind::Application, "terminal", "2.0.0", "terminal.elf", false, &candidate));
  assert(decidePackageVersion(candidate, nullptr) == InstallDecision::FreshInstall);
  assert(makeIdentity(Kind::Application, "terminal", "1.0.0", "terminal.elf", false, &installed));
  assert(decidePackageVersion(candidate, &installed) == InstallDecision::Upgrade);
  assert(decidePackageVersion(installed, &candidate) == InstallDecision::DowngradeBlocked);
  assert(decidePackageVersion(installed, &candidate, true) == InstallDecision::DowngradeAllowed);
  assert(decidePackageVersion(candidate, &candidate) == InstallDecision::AlreadyInstalled);
  installed.kind = Kind::Driver;
  assert(decidePackageVersion(candidate, &installed) == InstallDecision::IdentityConflict);
  installed.kind = Kind::Application;
  installed.legacyVersion = true;
  installed.version[0] = '\0';
  assert(decidePackageVersion(candidate, &installed) == InstallDecision::LegacyMigration);
  candidate.legacyVersion = true;
  assert(decidePackageVersion(candidate, &installed) == InstallDecision::InvalidCandidate);
}
} // namespace

int main() {
  acceptsFourKinds();
  rejectsIdentityAndCompatibility();
  rejectsUnsafeEntries();
  rejectsInvalidDependenciesBeforeLookup();
  versionPolicy();
  std::puts("Package preflight: four kinds, bounded entries, dependencies and numeric version policy passed");
}
