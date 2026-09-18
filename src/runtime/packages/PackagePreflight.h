#pragma once

#include "PackageIdentity.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace RuntimePackages {

// These are metadata limits, not a package decoder or allocation request.
// The caller must independently bound JSON/archive input before creating a view.
constexpr size_t kMaxPackageEntries = 16;
constexpr size_t kMaxPackageRequirements = 16;

struct PackageEntry {
  const char* name;
  uint64_t sizeBytes;
  const char* sha256;
  bool executable;
};

struct PackageRequirement {
  const char* capability;
  uint32_t minApi;
};

struct PackageEnvelopeView {
  Identity identity;
  const char* architecture;
  uint32_t minRuntimeApi;
  uint32_t securityVersion;
  const PackageEntry* entries;
  size_t entryCount;
  const PackageRequirement* requirements;
  size_t requirementCount;
};

struct PackageRuntimePolicy {
  const char* architecture;
  uint32_t runtimeApi;
  // Zero explicitly disables the optional experimental security-version
  // policy for ORDINARY integrity-checked packages. A nonzero threshold keeps
  // existing signed-package/rollback behavior unchanged.
  uint32_t minimumSecurityVersion;
  uint64_t maxEntryBytes;
  uint64_t maxTotalBytes;
};

enum class PreflightResult : uint8_t {
  ReadyForContentVerification,
  InvalidIdentity,
  UnsupportedArchitecture,
  IncompatibleRuntime,
  SecurityRollback,
  InvalidEntryList,
  InvalidEntry,
  DuplicateEntry,
  MissingExecutable,
  ResourceBudgetExceeded,
  InvalidRequirement,
  DuplicateRequirement,
  UnavailableCapability
};

// Canonical lowercase names avoid case-insensitive SD/FAT aliases; rejecting
// trailing punctuation avoids another source of basename normalization.
inline bool safePackageEntryName(const char* name) {
  if (!name) return false;
  size_t i = 0;
  for (; i < sizeof(Identity::artifact) && name[i]; ++i) {
    const char ch = name[i];
    const bool alnum = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
    if (!alnum && (i == 0 || (ch != '-' && ch != '_' && ch != '.'))) return false;
    if (ch == '.' && i && name[i - 1] == '.') return false;
  }
  if (!i || i >= sizeof(Identity::artifact)) return false;
  const char last = name[i - 1];
  return (last >= 'a' && last <= 'z') || (last >= '0' && last <= '9');
}

inline bool safePackageCapability(const char* capability) {
  if (!capability) return false;
  size_t i = 0;
  for (; i < 64 && capability[i]; ++i) {
    const char ch = capability[i];
    const bool alnum = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
    if (!alnum && (i == 0 || (ch != '.' && ch != '-' && ch != '_'))) return false;
    if (ch == '.' && i && capability[i - 1] == '.') return false;
  }
  if (!i || i >= 64) return false;
  const char last = capability[i - 1];
  return last != '.' && last != '-' && last != '_';
}

inline bool validSha256Hex(const char* digest) {
  if (!digest) return false;
  // Manifest strings can be shorter than 64 bytes. Detect the terminator
  // during validation rather than reading through a short JSON allocation.
  for (size_t i = 0; i != 64; ++i) {
    const char ch = digest[i];
    if (!ch || !((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')))
      return false;
  }
  return digest[64] == '\0';
}

// Resolver is a read-only lookup: uint32_t resolver(const char* capability).
// This validates *declarations* and available ABI contracts only. A successful
// result MUST NOT grant capabilities, authorize an ELF, install any content, or
// imply SHA-256 was computed. A caller must independently hash the payloads
// against declared digests, retain exact candidate bytes through staging, and
// apply execution-context grants at LOAD time. An ordinary digest is NOT a
// publisher signature. Optional signed packages may enforce additional trust.
template <typename Resolver>
PreflightResult preflightPackage(const PackageEnvelopeView& package,
                                 const PackageRuntimePolicy& runtime,
                                 Resolver resolver) {
  switch (package.identity.kind) {
    case Kind::Application: case Kind::Driver: case Kind::Service: case Kind::Provider: break;
    default: return PreflightResult::InvalidIdentity;
  }
  Identity canonical{};
  if (!makeIdentity(package.identity.kind, package.identity.id,
                    package.identity.version, package.identity.artifact, false, &canonical) ||
      package.identity.legacyVersion) return PreflightResult::InvalidIdentity;
  if (!package.architecture || !runtime.architecture ||
      std::strcmp(package.architecture, runtime.architecture) != 0)
    return PreflightResult::UnsupportedArchitecture;
  if (!package.minRuntimeApi || package.minRuntimeApi > runtime.runtimeApi)
    return PreflightResult::IncompatibleRuntime;
  if (runtime.minimumSecurityVersion &&
      (!package.securityVersion || package.securityVersion < runtime.minimumSecurityVersion))
    return PreflightResult::SecurityRollback;
  if (!package.entries || !package.entryCount || package.entryCount > kMaxPackageEntries ||
      !runtime.maxEntryBytes || !runtime.maxTotalBytes)
    return PreflightResult::InvalidEntryList;

  uint64_t total = 0;
  size_t executableCount = 0;
  for (size_t i = 0; i < package.entryCount; ++i) {
    const PackageEntry& entry = package.entries[i];
    if (!safePackageEntryName(entry.name) || !validSha256Hex(entry.sha256) ||
        !entry.sizeBytes || entry.sizeBytes > runtime.maxEntryBytes)
      return PreflightResult::InvalidEntry;
    for (size_t j = 0; j < i; ++j)
      if (std::strcmp(package.entries[j].name, entry.name) == 0)
        return PreflightResult::DuplicateEntry;
    const size_t length = std::strlen(entry.name);
    const bool elfSuffix = length >= 4 && std::strcmp(entry.name + length - 4, ".elf") == 0;
    if (entry.executable) {
      if (!elfSuffix || std::strcmp(entry.name, package.identity.artifact) != 0)
        return PreflightResult::InvalidEntry;
      ++executableCount;
    } else if (elfSuffix) {
      // An ELF may never hide among unverified resources.
      return PreflightResult::InvalidEntry;
    }
    if (entry.sizeBytes > runtime.maxTotalBytes - total)
      return PreflightResult::ResourceBudgetExceeded;
    total += entry.sizeBytes;
  }
  if (executableCount != 1) return PreflightResult::MissingExecutable;
  if (package.requirementCount > kMaxPackageRequirements ||
      (package.requirementCount && !package.requirements))
    return PreflightResult::InvalidRequirement;
  // Validate *all* declarations before consulting the capability registry.
  for (size_t i = 0; i < package.requirementCount; ++i) {
    const PackageRequirement& req = package.requirements[i];
    if (!safePackageCapability(req.capability) || !req.minApi)
      return PreflightResult::InvalidRequirement;
    for (size_t j = 0; j < i; ++j)
      if (std::strcmp(package.requirements[j].capability, req.capability) == 0)
        return PreflightResult::DuplicateRequirement;
  }
  for (size_t i = 0; i < package.requirementCount; ++i) {
    const PackageRequirement& req = package.requirements[i];
    if (resolver(req.capability) < req.minApi) return PreflightResult::UnavailableCapability;
  }
  return PreflightResult::ReadyForContentVerification;
}

// Numeric comparison avoids "1.10.0" sorting before "1.9.0". All version
// components must fit uint32_t; malformed or absent versions are incomparable.
enum class VersionOrder : uint8_t { Invalid, Older, Equal, Newer };

inline bool parsePackageVersion(const char* text, uint32_t (&parts)[3]) {
  parts[0] = parts[1] = parts[2] = 0;
  if (!safeVersion(text)) return false;
  size_t index = 0;
  for (const char* p = text; *p; ++p) {
    if (*p == '.') { ++index; continue; }
    const uint32_t digit = static_cast<uint32_t>(*p - '0');
    if (parts[index] > (std::numeric_limits<uint32_t>::max() - digit) / 10) return false;
    parts[index] = parts[index] * 10 + digit;
  }
  return true;
}

inline VersionOrder comparePackageVersions(const char* candidate, const char* installed) {
  uint32_t a[3]{}, b[3]{};
  if (!parsePackageVersion(candidate, a) || !parsePackageVersion(installed, b))
    return VersionOrder::Invalid;
  for (size_t i = 0; i < 3; ++i) {
    if (a[i] < b[i]) return VersionOrder::Older;
    if (a[i] > b[i]) return VersionOrder::Newer;
  }
  return VersionOrder::Equal;
}

enum class InstallDecision : uint8_t {
  InvalidCandidate, IdentityConflict, FreshInstall, Upgrade, LegacyMigration,
  AlreadyInstalled, DowngradeBlocked, DowngradeAllowed, InvalidInstalledVersion
};

inline InstallDecision decidePackageVersion(const Identity& candidate,
                                             const Identity* installed,
                                             bool allowDowngrade = false) {
  Identity canonical{};
  if (candidate.legacyVersion ||
      !makeIdentity(candidate.kind, candidate.id, candidate.version,
                    candidate.artifact, false, &canonical))
    return InstallDecision::InvalidCandidate;
  if (!installed) return InstallDecision::FreshInstall;
  if (!samePackage(candidate, *installed)) return InstallDecision::IdentityConflict;
  if (installed->legacyVersion && installed->version[0] == '\0')
    return InstallDecision::LegacyMigration;
  switch (comparePackageVersions(candidate.version, installed->version)) {
    case VersionOrder::Newer: return InstallDecision::Upgrade;
    case VersionOrder::Equal: return InstallDecision::AlreadyInstalled;
    case VersionOrder::Older:
      return allowDowngrade ? InstallDecision::DowngradeAllowed : InstallDecision::DowngradeBlocked;
    default: return InstallDecision::InvalidInstalledVersion;
  }
}

} // namespace RuntimePackages
