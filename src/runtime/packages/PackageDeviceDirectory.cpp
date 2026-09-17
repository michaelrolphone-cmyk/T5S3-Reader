#include "PackageDeviceDirectory.h"
#include "PackageDeviceSecurityFloor.h"
#include "PackageDeviceExtract.h"

#include <HalStorage.h>
#include <esp_task_wdt.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace RuntimePackages {
namespace {
constexpr size_t kPathCapacity = 256;

const char* kindRoot(Kind kind) {
  switch (kind) {
    case Kind::Application: return "/Apps";
    case Kind::Driver: return "/Drivers";
    case Kind::Service: return "/Services";
    case Kind::Provider: return "/Providers";
    default: return nullptr;
  }
}

// Reject traversal, foreign roots, other package IDs and arbitrary system
// files. The caller must independently authorize this identity and lifecycle.
bool permittedPath(const char* path, Kind kind, const char* id) {
  const char* root = kindRoot(kind);
  if (!path || !root || !safeId(id)) return false;
  if (std::strcmp(path, kPackageExtractStage) == 0) return true;
  char expected[kPathCapacity]{};
  int length = std::snprintf(expected, sizeof(expected), "%s/%s", root, id);
  if (length <= 0 || static_cast<size_t>(length) >= sizeof(expected)) return false;
  if (std::strcmp(path, expected) == 0) return true;
  length = std::snprintf(expected, sizeof(expected), "%s/.%s.previous", root, id);
  return length > 0 && static_cast<size_t>(length) < sizeof(expected) &&
         std::strcmp(path, expected) == 0;
}

bool childPath(const char* directory, const char* name,
               char (&output)[kPathCapacity]) {
  if (!directory || !name ||
      (std::strcmp(name, kPackageProvenanceName) && !safePackageEntryName(name)))
    return false;
  const int length = std::snprintf(output, sizeof(output), "%s/%s", directory, name);
  return length > 0 && static_cast<size_t>(length) < sizeof(output);
}

// The exact inventory check is required; trusting a signed list of two files
// cannot silently permit an extra ELF, directory, symlink-like entry or alias.
bool exactDirectoryEntries(const char* path, const PackageArchive& archive) {
  HalFile dir = Storage.open(path, O_RDONLY);
  if (!dir.isOpen() || !dir.isDirectory()) return false;
  bool seen[kMaxPackageEntries]{};
  bool provenanceSeen = false;
  size_t observed = 0;
  bool valid = true;
  while (valid) {
    HalFile entry = dir.openNextFile();
    if (!entry.isOpen()) break;
    char name[128]{};
    const size_t size = entry.getName(name, sizeof(name));
    const bool file = !entry.isDirectory();
    (void)entry.close();
    if (!file || !size || size >= sizeof(name)) { valid = false; break; }
    ++observed;
    if (std::strcmp(name, kPackageProvenanceName) == 0) {
      if (provenanceSeen) valid = false;
      provenanceSeen = true;
      continue;
    }
    bool matched = false;
    for (size_t i = 0; i < archive.entryCount; ++i) {
      if (std::strcmp(name, archive.entries[i].name) != 0) continue;
      if (seen[i]) valid = false;
      seen[i] = true;
      matched = true;
      break;
    }
    if (!matched) valid = false;
  }
  if (!dir.close() || !valid || !provenanceSeen ||
      observed != static_cast<size_t>(archive.entryCount) + 1) return false;
  for (size_t i = 0; i < archive.entryCount; ++i) if (!seen[i]) return false;
  return true;
}
} // namespace

ProvenanceResult verifySignedDeviceDirectory(const char* storagePath,
    Kind expectedKind, const char* expectedId,
    const TrustedPackageSigner* signers, size_t signerCount,
    const PackageRuntimePolicy& policy, PackageCapabilityApi resolveCapability,
    void* resolverContext, PackageVerificationWorkspace& workspace,
    PackageArchive& result, const uint8_t* expectedSignedPrefixDigest,
    PackageArchiveLimits limits, bool allowFirstInstall) {
  result = {};
  if (!permittedPath(storagePath, expectedKind, expectedId) ||
      !signers || !signerCount || signerCount > 16 || !Storage.ready())
    return ProvenanceResult::InvalidInput;
  char provenancePath[kPathCapacity]{};
  if (!childPath(storagePath, kPackageProvenanceName, provenancePath))
    return ProvenanceResult::InvalidInput;
  HalFile metadata = Storage.open(provenancePath, O_RDONLY);
  if (!metadata.isOpen() || metadata.isDirectory()) return ProvenanceResult::ReadFailure;
  const uint64_t length = metadata.fileSize64();
  auto provenanceRead = [&metadata, length](uint64_t offset, uint8_t* output,
                                            size_t count) {
    return output && offset <= length && count <= length - offset &&
           metadata.seek64(offset) &&
           metadata.read(output, count) == static_cast<int>(count);
  };
  // One entry handle is reused for each 512-byte chunk to avoid thousands of
  // SD open/close cycles when hashing a 1 MiB ELF.
  HalFile payload;
  char active[128]{};
  const auto openEntry = [storagePath, &payload, &active](const char* name) {
    if (!name || !safePackageEntryName(name)) return false;
    if (payload.isOpen() && std::strcmp(name, active) == 0) return true;
    if (payload.isOpen() && !payload.close()) return false;
    active[0] = '\0';
    char path[kPathCapacity]{};
    if (!childPath(storagePath, name, path)) return false;
    payload = Storage.open(path, O_RDONLY);
    if (!payload.isOpen() || payload.isDirectory()) return false;
    std::memcpy(active, name, std::strlen(name) + 1);
    return true;
  };
  const auto entrySize = [&payload, &openEntry](const char* name,
                                                uint64_t& observed) {
    if (!openEntry(name)) return false;
    observed = payload.fileSize64();
    return true;
  };
  const auto entryRead = [&payload, &openEntry](const char* name, uint64_t at,
                                                uint8_t* output, size_t bytes) {
    if (!output || !openEntry(name) || !payload.seek64(at)) return false;
    const bool read = payload.read(output, bytes) == static_cast<int>(bytes);
    if (read && (at & 0x3fffu) < bytes) (void)esp_task_wdt_reset();
    return read;
  };
  PackageMbedtlsSha256 hash;
  PackageDeviceTrustVerifier verifier(result, signers, signerCount);
  const ProvenanceResult status = verifySignedPackageDirectory(provenanceRead,
      length, entrySize, entryRead,
      [storagePath](const PackageArchive& archive) {
        return exactDirectoryEntries(storagePath, archive);
      }, hash, verifier,
      [resolveCapability, resolverContext](const char* capability) -> uint32_t {
        return resolveCapability ? resolveCapability(capability, resolverContext) : 0;
      },
      [allowFirstInstall](const PackageArchive& archive) {
        return checkPackageSecurityFloor(devicePackageSecurityFloors(), archive,
                                         allowFirstInstall) == FloorCheck::Allowed;
      }, policy, result, workspace, expectedSignedPrefixDigest, limits);
  const bool closed = !payload.isOpen() || payload.close();
  const bool metadataClosed = metadata.close();
  if (status != ProvenanceResult::AuthenticatedDirectory || !closed || !metadataClosed ||
      result.identity.kind != expectedKind ||
      std::strcmp(result.identity.id, expectedId) != 0) {
    result = {};
    return status == ProvenanceResult::AuthenticatedDirectory ?
        ProvenanceResult::PolicyRejected : status;
  }
  return ProvenanceResult::AuthenticatedDirectory;
}

} // namespace RuntimePackages
