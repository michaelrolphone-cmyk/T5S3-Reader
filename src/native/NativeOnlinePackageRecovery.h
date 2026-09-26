#pragma once

#include "runtime/packages/PackageOrdinarySdAdapter.h"
#include "runtime/packages/PackageOrdinaryManifest.h"
#include "runtime/packages/PackageOrdinaryTransaction.h"
#include <HalStorage.h>
#include <cstring>
#include <memory>
#include <new>
#include <string>

namespace RuntimeOnlinePackages {
namespace Recovery {

// Online App Store installs never resume partial downloads. Before each new
// attempt, discard only the deterministic manager-owned scratch for that app.
// Unknown files or subdirectories are never removed.
inline bool discardOwnedInbox(const std::string& root,
    const std::string& sidecarName, const std::string& artifact) {
  if (!RuntimePackages::safePackageEntryName(sidecarName.c_str()) ||
      !RuntimePackages::safePackageEntryName(artifact.c_str())) return false;
  if (!Storage.exists(root.c_str())) return true;

  HalFile directory = Storage.open(root.c_str(), O_RDONLY);
  if (!directory.isOpen() || !directory.isDirectory()) {
    if (directory.isOpen()) (void)directory.close();
    return false;
  }

  const std::string part = artifact + ".part";
  bool sidecarSeen = false, elfSeen = false, partSeen = false, descriptorSeen = false;
  bool valid = true;
  for (;;) {
    HalFile entry = directory.openNextFile();
    if (!entry.isOpen()) break;
    char name[128]{};
    const size_t n = entry.getName(name, sizeof(name));
    const bool regular = n && n < sizeof(name) && !entry.isDirectory();
    (void)entry.close();
    if (!regular) { valid = false; break; }

    if (sidecarName == name && !sidecarSeen) sidecarSeen = true;
    else if (artifact == name && !elfSeen) elfSeen = true;
    else if (part == name && !partSeen) partSeen = true;
    else if (!std::strcmp(name, RuntimePackages::kOrdinaryManifestName) &&
             !descriptorSeen) descriptorSeen = true;
    else { valid = false; break; }
  }

  const bool closed = directory.close();
  if (!valid || !closed) return false;

  if (partSeen && !Storage.remove((root + "/" + part).c_str())) return false;
  if (elfSeen && !Storage.remove((root + "/" + artifact).c_str())) return false;
  if (descriptorSeen &&
      !Storage.remove((root + "/" + RuntimePackages::kOrdinaryManifestName).c_str()))
    return false;
  if (sidecarSeen && !Storage.remove((root + "/" + sidecarName).c_str())) return false;
  return Storage.rmdir(root.c_str());
}

// The canonical package stage is also scratch owned by the package manager.
// Do not compare it to a previous attempt or try to resume it. If it contains
// only files named by the current plan (plus .package.json), remove it and
// rebuild the stage from the freshly downloaded source. Any unknown entry
// leaves the directory untouched and aborts the install.
inline bool discardOwnedStage(const RuntimePackages::OrdinaryPackagePlan& plan) {
  using namespace RuntimePackages;
  OrdinaryTransactionPaths paths{};
  if (!ordinaryTransactionPaths(plan.identity.kind, plan.identity.id, paths)) return false;
  if (!Storage.exists(paths.stage)) return true;

  HalFile directory = Storage.open(paths.stage, O_RDONLY);
  if (!directory.isOpen() || !directory.isDirectory()) {
    if (directory.isOpen()) (void)directory.close();
    return false;
  }

  bool seen[kMaxPackageEntries]{};
  bool manifestSeen = false;
  bool valid = true;
  for (;;) {
    HalFile entry = directory.openNextFile();
    if (!entry.isOpen()) break;
    char name[128]{};
    const size_t n = entry.getName(name, sizeof(name));
    const bool regular = n && n < sizeof(name) && !entry.isDirectory();
    (void)entry.close();
    if (!regular) { valid = false; break; }

    if (!std::strcmp(name, kOrdinaryManifestName)) {
      if (manifestSeen) { valid = false; break; }
      manifestSeen = true;
      continue;
    }

    size_t i = 0;
    while (i < plan.entryCount && std::strcmp(plan.entries[i].name, name)) ++i;
    if (i == plan.entryCount || seen[i]) { valid = false; break; }
    seen[i] = true;
  }

  const bool closed = directory.close();
  if (!valid || !closed) return false;

  for (size_t i = 0; i < plan.entryCount; ++i) {
    if (seen[i] &&
        !Storage.remove((std::string(paths.stage) + "/" + plan.entries[i].name).c_str()))
      return false;
  }
  if (manifestSeen &&
      !Storage.remove((std::string(paths.stage) + "/" + kOrdinaryManifestName).c_str()))
    return false;
  return Storage.rmdir(paths.stage);
}

} // namespace Recovery
} // namespace RuntimeOnlinePackages
