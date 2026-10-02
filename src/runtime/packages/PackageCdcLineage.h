#pragma once
#include "PackageIdentity.h"
namespace RuntimePackages {
// One explicit historical identity repair, never a general '-v2' rewrite.
constexpr const char* kCdcCanonicalId = "usb-cdc-acm";
constexpr const char* kCdcAliasId = "usb-cdc-acm-v2";
constexpr const char* kCdcCanonicalRoot = "/Drivers/usb-cdc-acm";
constexpr const char* kCdcAliasRoot = "/Drivers/usb-cdc-acm-v2";
constexpr const char* kCdcHoldingRoot = "/Drivers/.usb-cdc-acm-v2.pkg-migrating";
constexpr const char* kCdcIntentPath = "/Drivers/.usb-cdc-acm.migration";
constexpr const char* kCdcIntentPart = "/Drivers/.usb-cdc-acm.migration-part";
inline bool cdcLineage(Kind kind, const char* id) {
  return kind == Kind::Driver && id && (!std::strcmp(id, kCdcCanonicalId) || !std::strcmp(id, kCdcAliasId));
}
template <typename Ops>
bool cdcMigrationPending(Ops& ops) {
  return ops.exists(kCdcIntentPath) || ops.exists(kCdcIntentPart) || ops.exists(kCdcHoldingRoot);
}
template <typename Ops>
bool cdcLineageBlocked(Ops& ops, Kind kind, const char* id) {
  return cdcLineage(kind, id) && cdcMigrationPending(ops);
}
}  // namespace RuntimePackages
