#include "PackageOrdinarySdAdapter.h"
#include "PackageCdcSdMigration.h"
#include "PackageOrdinaryManifest.h"
#include "PackageOrdinaryTransaction.h"
#include "PackageOrdinarySdTree.h"
#include <memory>
#include <new>
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
  const std::string metadataPath = std::string(root) + "/" + kOrdinaryManifestName;
  std::unique_ptr<OrdinaryPackagePlan> plan(new (std::nothrow) OrdinaryPackagePlan{});
  if (!plan) return false;
  const bool hasManifest = Storage.exists(metadataPath.c_str());
  if (hasManifest) {
    HalFile metadata = Storage.open(metadataPath.c_str(), O_RDONLY);
    if (!metadata.isOpen()) return false;
    if (metadata.isDirectory()) { (void)metadata.close(); return false; }
    const uint64_t count = metadata.fileSize64();
    std::unique_ptr<char[]> buffer(new (std::nothrow) char[kManifestLimit]);
    const bool read = buffer && count && count <= kManifestLimit &&
        metadata.read(reinterpret_cast<uint8_t*>(buffer.get()), count) == static_cast<int>(count);
    const bool closed = metadata.close();
    if (!read || !closed || !parseOrdinaryManifest(buffer.get(), count, *plan) ||
        plan->identity.kind != kind || std::strcmp(plan->identity.id, id)) return false;
  }
  OrdinarySdTreeOps ops(root);
  // A manifest-free tombstone is removable only if the shared inventory finds
  // no files or directories. Unknown/user-owned data always prevents cleanup.
  return purgeOrdinaryTree(*plan, ops, false);
}
} // namespace

OrdinaryTransactionResult uninstallOrdinaryFromSd(Kind kind, const char* id,
    const PackageRuntimePolicy& policy,
    uint32_t (*resolveCapability)(const char*)) {
  OrdinaryTransactionPaths paths{};
  if (!Storage.ready() || !resolveCapability ||
      !ordinaryTransactionPaths(kind, id, paths))
    return OrdinaryTransactionResult::InvalidIdentity;
  if (cdcLineage(kind, id) && cdcMigrationPendingOnSd())
    return OrdinaryTransactionResult::AmbiguousState;
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
