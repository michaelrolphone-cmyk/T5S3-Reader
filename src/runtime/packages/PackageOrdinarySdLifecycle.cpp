#include "PackageOrdinarySdAdapter.h"
#include "PackageOrdinaryManifest.h"
#include "PackageOrdinaryTransaction.h"
#include <HalStorage.h>
#include <cstring>
#include <string>

namespace RuntimePackages {
namespace {
constexpr size_t kManifestLimit = 4096;
struct Ops {
  bool exists(const char* path) const { return Storage.exists(path); }
  bool rename(const char* from, const char* to) const {
    return Storage.rename(from, to);
  }
};

// Verify inventory before deleting a single byte, including in a partially
// purged tombstone. Unknown entries and subdirectories belong to someone else.
bool purgeKnownTombstone(const char* root, Kind kind, const char* id) {
  if (!root || !safeId(id)) return false;
  HalFile directory = Storage.open(root, O_RDONLY);
  if (!directory.isOpen() || !directory.isDirectory()) {
    if (directory.isOpen()) (void)directory.close();
    return false;
  }
  const std::string metadataPath = std::string(root) + "/" + kOrdinaryManifestName;
  OrdinaryPackagePlan plan{};
  bool hasManifest = Storage.exists(metadataPath.c_str());
  if (hasManifest) {
    HalFile metadata = Storage.open(metadataPath.c_str(), O_RDONLY);
    if (!metadata.isOpen() || metadata.isDirectory()) {
      if (metadata.isOpen()) (void)metadata.close();
      (void)directory.close();
      return false;
    }
    const uint64_t count = metadata.fileSize64();
    char buffer[kManifestLimit]{};
    const bool read = count && count <= sizeof(buffer) &&
        metadata.read(reinterpret_cast<uint8_t*>(buffer), count) == static_cast<int>(count);
    const bool closed = metadata.close();
    if (!read || !closed || !parseOrdinaryManifest(buffer, count, plan) ||
        plan.identity.kind != kind || std::strcmp(plan.identity.id, id)) {
      (void)directory.close();
      return false;
    }
  }
  bool seen[kMaxPackageEntries]{}, manifestSeen = false, okay = true;
  size_t count = 0;
  while (okay) {
    HalFile entry = directory.openNextFile();
    if (!entry.isOpen()) break;
    char name[128]{};
    const size_t length = entry.getName(name, sizeof(name));
    if (!length || length >= sizeof(name) || entry.isDirectory()) okay = false;
    else if (!std::strcmp(name, kOrdinaryManifestName)) {
      if (!hasManifest || manifestSeen) okay = false;
      manifestSeen = true;
    } else {
      bool declared = false;
      for (size_t i = 0; i < plan.entryCount; ++i) {
        if (std::strcmp(name, plan.entries[i].name)) continue;
        if (seen[i]) okay = false;
        seen[i] = declared = true;
        break;
      }
      if (!declared) okay = false;
    }
    (void)entry.close();
    if (++count > plan.entryCount + 1) okay = false;
  }
  const bool closed = directory.close();
  if (!closed || !okay || (hasManifest && !manifestSeen) ||
      (!hasManifest && count != 0)) return false;
  for (size_t i = 0; i < plan.entryCount; ++i) {
    const std::string filename = std::string(root) + "/" + plan.entries[i].name;
    if (Storage.exists(filename.c_str()) && !Storage.remove(filename.c_str())) return false;
  }
  // Manifest-last cleanup allows reboot recovery even if entry deletion
  // failed or power was removed in the middle of the transaction.
  if (hasManifest && !Storage.remove(metadataPath.c_str())) return false;
  return Storage.rmdir(root);
}
} // namespace

OrdinaryTransactionResult uninstallOrdinaryFromSd(Kind kind, const char* id,
    const PackageRuntimePolicy& policy,
    uint32_t (*resolveCapability)(const char*)) {
  OrdinaryTransactionPaths paths{};
  if (!Storage.ready() || !resolveCapability ||
      !ordinaryTransactionPaths(kind, id, paths))
    return OrdinaryTransactionResult::InvalidIdentity;
  Ops ops;
  Identity observed{};
  const auto verify = [&policy, resolveCapability](const char* path,
                                                   Identity& identity) {
    return verifyOrdinarySdDirectory(path, policy, resolveCapability, identity);
  };
  const auto purge = [kind, id](const char* path) {
    return purgeKnownTombstone(path, kind, id);
  };
  return uninstallOrdinaryPackage(ops, kind, id, verify, purge, observed);
}
} // namespace RuntimePackages
