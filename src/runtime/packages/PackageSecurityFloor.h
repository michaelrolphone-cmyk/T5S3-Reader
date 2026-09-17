#pragma once

#include "PackageArchive.h"

#include <cstdint>

namespace RuntimePackages {

// A floor belongs to (kind, ID), not a filename, semantic version or signer
// key. The backend MUST be device-controlled persistent storage, never a
// manifest, removable-SD sidecar or package-supplied callback.
enum class FloorRead : uint8_t { Present, NotEstablished, Unavailable };
enum class FloorAdvance : uint8_t { Advanced, AlreadyAtOrAbove, Downgrade, Unavailable };
enum class FloorCheck : uint8_t { Allowed, SecurityRollback, Unavailable, InvalidIdentity };

inline bool validFloorIdentity(const PackageArchive& candidate) {
  switch (candidate.identity.kind) {
    case Kind::Application: case Kind::Driver: case Kind::Service: case Kind::Provider: break;
    default: return false;
  }
  return safeId(candidate.identity.id) &&
         safeVersion(candidate.identity.version) && candidate.securityVersion != 0 &&
         !candidate.identity.legacyVersion;
}

// Store API: FloorRead read(Kind, const char*, uint32_t&). A corrupt record,
// backend error or inaccessible trust partition MUST return Unavailable. An
// absent record is distinct from an inaccessible store; the runtime must
// separately authorize first install before accepting NotEstablished.
template <typename Store>
FloorCheck checkPackageSecurityFloor(Store& store, const PackageArchive& candidate,
                                     bool allowFirstInstall) {
  if (!validFloorIdentity(candidate)) return FloorCheck::InvalidIdentity;
  uint32_t floor = 0;
  const FloorRead status = store.read(candidate.identity.kind, candidate.identity.id, floor);
  if (status == FloorRead::Unavailable) return FloorCheck::Unavailable;
  if (status == FloorRead::NotEstablished)
    return allowFirstInstall ? FloorCheck::Allowed : FloorCheck::Unavailable;
  if (!floor) return FloorCheck::Unavailable;
  return candidate.securityVersion < floor ? FloorCheck::SecurityRollback : FloorCheck::Allowed;
}

// Store API: FloorAdvance advance(Kind, const char*, uint32_t). The backend
// must atomically read/compare/commit against other writers and never lower a
// floor. Call ONLY after authenticated bytes are durably published, the target
// can be verified after restart, and recovery cannot require an older version
// that this floor would reject. This does not authorize publication itself.
template <typename Store>
FloorAdvance advancePackageSecurityFloor(Store& store, const PackageArchive& installed) {
  if (!validFloorIdentity(installed)) return FloorAdvance::Unavailable;
  return store.advance(installed.identity.kind, installed.identity.id,
                       installed.securityVersion);
}

} // namespace RuntimePackages
