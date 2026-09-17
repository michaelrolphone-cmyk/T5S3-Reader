#include "PackageDevicePublication.h"
#include "PackageDeviceSecurityFloor.h"

#include <HalStorage.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace RuntimePackages {
namespace {
constexpr size_t kPathBytes = 256;
std::mutex& publicationMutex() {
  // Serializes the one shared /Packages/.extract.part across package identities
  // as well as the separate target-specific PackageReplacementLease.
  static std::mutex mutex;
  return mutex;
}

bool childPath(const char* parent, const char* name, char (&out)[kPathBytes]) {
  if (!parent || !name ||
      (std::strcmp(name, kPackageProvenanceName) && !safePackageEntryName(name)))
    return false;
  const int bytes = std::snprintf(out, sizeof(out), "%s/%s", parent, name);
  return bytes > 0 && static_cast<size_t>(bytes) < sizeof(out);
}

struct StorageOps {
  bool exists(const char* path) { return Storage.exists(path); }
  bool rename(const char* from, const char* to) { return Storage.rename(from, to); }
};

// Backup cleanup can be interrupted after individual entry deletions; retain
// signed provenance until LAST, so on reboot we can authenticate the manifest
// and remove only listed remaining entries. An empty provenance-free directory
// is the sole special case for a cut between final remove and rmdir.
bool emptyDirectory(const char* path) {
  HalFile directory = Storage.open(path, O_RDONLY);
  if (!directory.isOpen() || !directory.isDirectory()) return false;
  HalFile item = directory.openNextFile();
  const bool empty = !item.isOpen();
  if (item.isOpen()) (void)item.close();
  return directory.close() && empty;
}

bool inspectBackupProvenance(const char* backup, Kind kind, const char* id,
    const TrustedPackageSigner* signers, size_t signerCount,
    PackageVerificationWorkspace& workspace, PackageArchive& archive,
    PackageArchiveLimits limits) {
  archive = {};
  if (!signers || !signerCount || signerCount > 16) return false;
  char recordPath[kPathBytes]{};
  if (!childPath(backup, kPackageProvenanceName, recordPath)) return false;
  HalFile record = Storage.open(recordPath, O_RDONLY);
  if (!record.isOpen() || record.isDirectory()) return false;
  const uint64_t size = record.fileSize64();
  uint8_t header[kPackageHeaderBytes]{};
  if (size > kPackageProvenanceLimit || size < sizeof(header) + 16 +
      kPackageSignatureBytes ||
      record.read(header, sizeof(header)) != static_cast<int>(sizeof(header)) ||
      std::memcmp(header, "RISCPKG1", 8)) return false;
  const uint32_t manifestBytes = ArchiveDetail::le32(header + 12);
  if (manifestBytes < 16 || manifestBytes > kPackageManifestLimit ||
      size != kPackageHeaderBytes + manifestBytes + kPackageSignatureBytes ||
      !record.seek64(0) ||
      record.read(workspace.signedPrefix, kPackageHeaderBytes + manifestBytes) !=
          static_cast<int>(kPackageHeaderBytes + manifestBytes) ||
      std::memcmp(header, workspace.signedPrefix, sizeof(header))) return false;
  const size_t prefixSize = kPackageHeaderBytes + manifestBytes;
  const auto prefix = [&workspace, prefixSize](uint64_t at, uint8_t* out, size_t count) {
    if (at > prefixSize || count > prefixSize - at) return false;
    std::memcpy(out, workspace.signedPrefix + at, count);
    return true;
  };
  if (decodePackageArchive(prefix, ArchiveDetail::le64(header + 36), archive,
      workspace.manifestScratch, sizeof(workspace.manifestScratch), limits) !=
          ArchiveResult::ReadyForAuthentication ||
      archive.identity.kind != kind || std::strcmp(archive.identity.id, id)) {
    archive = {};
    return false;
  }
  PackageMbedtlsSha256 hash;
  uint8_t digest[32]{};
  uint8_t signature[kPackageSignatureBytes]{};
  PackageDeviceTrustVerifier verifier(archive, signers, signerCount);
  const bool trusted = hash.start() &&
      hash.update(workspace.signedPrefix, prefixSize) && hash.finish(digest) &&
      record.read(signature, sizeof(signature)) == static_cast<int>(sizeof(signature)) &&
      verifier(archive.keyId, digest, signature) && record.close();
  if (!trusted) archive = {};
  return trusted;
}

bool selectivePurgeBackup(const char* path, Kind kind, const char* id,
    const TrustedPackageSigner* signers, size_t signerCount,
    PackageVerificationWorkspace& workspace, PackageArchive& scratch,
    PackageArchiveLimits limits) {
  SignedTransactionPaths paths{};
  if (!signedTransactionPaths(kind, id, paths) ||
      !path || std::strcmp(path, paths.backup) || !Storage.ready()) return false;
  char provenance[kPathBytes]{};
  if (!childPath(path, kPackageProvenanceName, provenance)) return false;
  if (!Storage.exists(provenance)) {
    // Only the final rmdir may remain if provenance was removed last.
    return emptyDirectory(path) && Storage.rmdir(path);
  }
  if (!inspectBackupProvenance(path, kind, id, signers, signerCount,
                               workspace, scratch, limits)) return false;
  HalFile directory = Storage.open(path, O_RDONLY);
  if (!directory.isOpen() || !directory.isDirectory()) return false;
  bool recognized = true;
  while (recognized) {
    HalFile item = directory.openNextFile();
    if (!item.isOpen()) break;
    char name[128]{};
    const size_t nameBytes = item.getName(name, sizeof(name));
    const bool regular = !item.isDirectory();
    (void)item.close();
    if (!regular || !nameBytes || nameBytes >= sizeof(name)) {
      recognized = false;
      break;
    }
    if (std::strcmp(name, kPackageProvenanceName) == 0) continue;
    bool signedName = false;
    for (size_t i = 0; i < scratch.entryCount; ++i)
      if (std::strcmp(name, scratch.entries[i].name) == 0) signedName = true;
    if (!signedName) recognized = false;
  }
  if (!directory.close() || !recognized) return false;
  for (size_t i = 0; i < scratch.entryCount; ++i) {
    char file[kPathBytes]{};
    if (!childPath(path, scratch.entries[i].name, file)) return false;
    if (Storage.exists(file) && !Storage.remove(file)) return false;
  }
  // Signed metadata is removed only when all known payload files are gone.
  if (!emptyDirectory(path)) {
    HalFile metadata = Storage.open(provenance, O_RDONLY);
    if (!metadata.isOpen() || metadata.isDirectory()) return false;
    if (!metadata.close()) return false;
  }
  if (!Storage.remove(provenance)) return false;
  return Storage.rmdir(path);
}

bool validInputs(const TrustedPackageSigner* signers, size_t count,
                 const PackageRuntimePolicy& policy) {
  return Storage.ready() && signers && count && count <= 16 &&
         policy.architecture && policy.runtimeApi;
}

// The scratch archive belongs to the manager's shared serialized purge, not
// an app or a small FreeRTOS task stack. It must never alias the current
// generation that the transaction will use for the NVS commit.
PackageArchive& backupScratch() {
  static PackageArchive archive{};
  return archive;
}

bool ensureRoot(Kind kind, const char* id) {
  SignedTransactionPaths paths{};
  if (!signedTransactionPaths(kind, id, paths)) return false;
  char root[16]{};
  const char* slash = std::strrchr(paths.target, '/');
  if (!slash || static_cast<size_t>(slash - paths.target) >= sizeof(root)) return false;
  std::memcpy(root, paths.target, static_cast<size_t>(slash - paths.target));
  if (!Storage.exists(root) && !Storage.mkdir(root)) return false;
  HalFile directory = Storage.open(root, O_RDONLY);
  if (!directory.isOpen() || !directory.isDirectory()) return false;
  return directory.close();
}
} // namespace

SignedTransactionResult publishSignedDevicePackage(const PackageArchive& approved,
    const uint8_t expectedSignedPrefixDigest[32],
    const TrustedPackageSigner* signers, size_t signerCount,
    const PackageRuntimePolicy& policy, PackageCapabilityApi resolveCapability,
    void* resolverContext, PackageVerificationWorkspace& workspace,
    PackageArchive& observed, PackageArchiveLimits limits,
    bool allowFirstInstall, bool allowSemverDowngrade) {
  if (&approved == &observed || !expectedSignedPrefixDigest ||
      !validInputs(signers, signerCount, policy) ||
      !ensureRoot(approved.identity.kind, approved.identity.id))
    return SignedTransactionResult::InvalidInput;
  std::lock_guard<std::mutex> lock(publicationMutex());
  StorageOps ops;
  const auto verify = [=, &workspace](const char* path, const uint8_t* digest,
                                       PackageArchive& out) {
    return verifySignedDeviceDirectory(path, approved.identity.kind,
        approved.identity.id, signers, signerCount, policy, resolveCapability,
        resolverContext, workspace, out, digest, limits, allowFirstInstall) ==
        ProvenanceResult::AuthenticatedDirectory;
  };
  const auto purge = [=, &workspace](const char* path) {
    return selectivePurgeBackup(path, approved.identity.kind,
        approved.identity.id, signers, signerCount, workspace,
        backupScratch(), limits);
  };
  return publishSignedDirectoryTransaction(ops, approved,
      expectedSignedPrefixDigest, verify, purge, devicePackageSecurityFloors(),
      observed, allowFirstInstall, allowSemverDowngrade);
}

SignedTransactionResult recoverSignedDevicePackage(Kind kind, const char* id,
    const TrustedPackageSigner* signers, size_t signerCount,
    const PackageRuntimePolicy& policy, PackageCapabilityApi resolveCapability,
    void* resolverContext, PackageVerificationWorkspace& workspace,
    PackageArchive& observed, PackageArchiveLimits limits,
    bool allowFirstInstall) {
  if (!signedTransactionPaths(kind, id, *([]() -> SignedTransactionPaths* {
          static SignedTransactionPaths paths{}; return &paths;
        })()) || !validInputs(signers, signerCount, policy))
    return SignedTransactionResult::InvalidInput;
  std::lock_guard<std::mutex> lock(publicationMutex());
  StorageOps ops;
  const auto verify = [=, &workspace](const char* path, const uint8_t* digest,
                                       PackageArchive& out) {
    return verifySignedDeviceDirectory(path, kind, id, signers, signerCount,
        policy, resolveCapability, resolverContext, workspace, out, digest,
        limits, allowFirstInstall) == ProvenanceResult::AuthenticatedDirectory;
  };
  const auto purge = [=, &workspace](const char* path) {
    return selectivePurgeBackup(path, kind, id, signers, signerCount,
                                workspace, backupScratch(), limits);
  };
  return recoverSignedDirectoryTransaction(ops, kind, id, verify, purge,
      devicePackageSecurityFloors(), observed, allowFirstInstall);
}

} // namespace RuntimePackages
