#include "runtime/packages/PackageTrustPolicy.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace RuntimePackages;
int main() {
  PackageArchive archive{};
  assert(makeIdentity(Kind::Driver, "gps-nmea", "1.0.0", "driver.elf", false,
                      &archive.identity));
  archive.signatureAlgorithm = kPackageSignatureP256Sha256;
  archive.securityVersion = 4;
  uint8_t key[65]{};
  key[0] = 4;
  TrustedPackageSigner keys[] = {{7, key, sizeof(key), Kind::Driver, "gps-nmea", 3, false}};
  assert(selectTrustedPackageSigner(archive, 7, keys, 1) == &keys[0]);
  assert(!selectTrustedPackageSigner(archive, 8, keys, 1));
  assert(!selectTrustedPackageSigner(archive, 7, nullptr, 0));
  assert(!selectTrustedPackageSigner(archive, 7, keys, 17));
  keys[0].revoked = true;
  assert(!selectTrustedPackageSigner(archive, 7, keys, 1));
  keys[0].revoked = false;
  keys[0].allowedKind = Kind::Application;
  assert(!selectTrustedPackageSigner(archive, 7, keys, 1));
  keys[0].allowedKind = Kind::Driver;
  keys[0].allowedPackageId = "usb-cdc-acm";
  assert(!selectTrustedPackageSigner(archive, 7, keys, 1));
  keys[0].allowedPackageId = "gps-nmea";
  keys[0].minimumSecurityVersion = 5;
  assert(!selectTrustedPackageSigner(archive, 7, keys, 1));
  keys[0].minimumSecurityVersion = 3;
  key[0] = 0;
  assert(!selectTrustedPackageSigner(archive, 7, keys, 1));
  key[0] = 4;
  keys[0].publicPointLength = 64;
  assert(!selectTrustedPackageSigner(archive, 7, keys, 1));
  keys[0].publicPointLength = sizeof(key);
  keys[0].allowedPackageId = "../gps";
  assert(!selectTrustedPackageSigner(archive, 7, keys, 1));
  keys[0].allowedPackageId = "gps-nmea";
  TrustedPackageSigner duplicate[] = {keys[0], keys[0]};
  duplicate[1].revoked = true;
  assert(!selectTrustedPackageSigner(archive, 7, duplicate, 2));
  archive.signatureAlgorithm = 2;
  assert(!selectTrustedPackageSigner(archive, 7, keys, 1));
  archive.signatureAlgorithm = kPackageSignatureP256Sha256;
  archive.identity.id[0] = 'G';
  assert(!selectTrustedPackageSigner(archive, 7, keys, 1));
  std::puts("Package trust policy: key scope, revocation, rollback floor and duplicate IDs passed");
}
