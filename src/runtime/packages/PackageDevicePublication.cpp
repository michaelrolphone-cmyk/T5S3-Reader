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
  // Serializes the shared /Packages/.extract.part across identities, alongside
  // the target-specific PackageReplacementLease held by the transaction.
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

bool emptyDirectory(const char* path) {
  HalFile dir = Storage.open(path, O_RDONLY);
  if (!dir.isOpen() || !dir.isDirectory()) return false;
  HalFile entry = dir.openNextFile();
  const bool empty = !entry.isOpen();
  if (entry.isOpen()) (void)entry.close();
  return dir.close() && empty;
}

bool onlyProvenance(const char* path) {
  HalFile dir = Storage.open(path, O_RDONLY);
  if (!dir.isOpen() || !dir.isDirectory()) return false;
  size_t found = 0;
  bool valid = true;
  while (valid) {
    HalFile entry = dir.openNextFile();
    if (!entry.isOpen()) break;
    char name[128]{};
    const size_t length = entry.getName(name, sizeof(name));
    const bool regular = !entry.isDirectory();
    (void)entry.close();
    if (!regular || !length || length >= sizeof(name) ||
        std::strcmp(name, kPackageProvenanceName) || ++found > 1)
      valid = false;
  }
  return dir.close() && valid && found == 1;
}

// Backup cleanup is resumable even after an interrupted per-file deletion.
// Retain signed metadata until LAST, so only filenames listed in an actually
// authenticated prior manifest can be deleted. The sole provenance-free
// recovery state is an empty directory awaiting final rmdir.
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
  const auto prefix = [&workspace, prefixSize](uint64_t at, uint8_t* out,
                                                size_t count) {
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
  if (!Storage.exists(provenance))
    return emptyDirectory(path) && Storage.rmdir(path);
  if (!inspectBackupProvenance(path, kind, id, signers, signerCount,
                               workspace, scratch, limits)) return false;
  HalFile dir = Storage.open(path, O_RDONLY);
  if (!dir.isOpen() || !dir.isDirectory()) return false;
  bool recognized = true;
  while (recognized) {
    HalFile item = dir.openNextFile();
    if (!item.isOpen()) break;
    char name[128]{};
    const size_t length = item.getName(name, sizeof(name));
    const bool regular = !item.isDirectory();
    (void)item.close();
    if (!regular || !length || length >= sizeof(name)) {
      recognized = false;
      break;
    }
    if (std::strcmp(name, kPackageProvenanceName) == 0) continue;
    bool signedName = false;
    for (size_t i = 0; i < scratch.entryCount; ++i)
      if (std::strcmp(name, scratch.entries[i].name) == 0) signedName = true;
    if (!signedName) recognized = false;
  }
  if (!dir.close() || !recognized) return false;
  for (size_t i = 0; i < scratch.entryCount; ++i) {
    char file[kPathBytes]{};
    if (!childPath(path, scratch.entries[i].name, file)) return false;
    if (Storage.exists(file) && !Storage.remove(file)) return false;
  }
  // Do not discard provenance if unexpected content appeared during cleanup.
  if (!onlyProvenance(path) || !Storage.remove(provenance)) return false;
  return Storage.rmdir(path);
}

bool validInputs(const TrustedPackageSigner* signers, size_t count,
                 const PackageRuntimePolicy& policy) {
  return Storage.ready() && signers && count && count <= 16 &&
         policy.architecture && policy.runtimeApi;
}

// This scratch is used only while the publication-wide mutex is held. Do not
// alias the observed target archive used for the subsequent NVS advancement.
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
      !validInputs(signers, signerCount, policy))
    return SignedTransactionResult::InvalidInput;
  std::lock_guard<std::mutex> lock(publicationMutex());
  if (!ensureRoot(approved.identity.kind, approved.identity.id))
    return SignedTransactionResult::InvalidInput;
  StorageOps ops;
  const auto verify = [&approved, signers, signerCount, policy,
      resolveCapability, resolverContext, &workspace, limits, allowFirstInstall]
      (const char* path, const uint8_t* digest, PackageArchive& out) {
    return verifySignedDeviceDirectory(path, approved.identity.kind,
        approved.identity.id, signers, signerCount, policy, resolveCapability,
        resolverContext, workspace, out, digest, limits, allowFirstInstall) ==
        ProvenanceResult::AuthenticatedDirectory;
  };
  const auto purge = [&approved, signers, signerCount, &workspace, limits]
      (const char* path) {
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
  SignedTransactionPaths paths{};
  if (!signedTransactionPaths(kind, id, paths) ||
      !validInputs(signers, signerCount, policy))
    return SignedTransactionResult::InvalidInput;
  std::lock_guard<std::mutex> lock(publicationMutex());
  StorageOps ops;
  const auto verify = [kind, id, signers, signerCount, policy,
      resolveCapability, resolverContext, &workspace, limits, allowFirstInstall]
      (const char* path, const uint8_t* digest, PackageArchive& out) {
    return verifySignedDeviceDirectory(path, kind, id, signers, signerCount,
        policy, resolveCapability, resolverContext, workspace, out, digest,
        limits, allowFirstInstall) == ProvenanceResult::AuthenticatedDirectory;
  };
  const auto purge = [kind, id, signers, signerCount, &workspace, limits]
      (const char* path) {
    return selectivePurgeBackup(path, kind, id, signers, signerCount,
                                workspace, backupScratch(), limits);
  };
  return recoverSignedDirectoryTransaction(ops, kind, id, verify, purge,
      devicePackageSecurityFloors(), observed, allowFirstInstall);
}

} // namespace RuntimePackages
