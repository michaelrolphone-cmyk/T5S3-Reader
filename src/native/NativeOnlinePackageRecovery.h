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

// Driver intake still uses exact-match interrupted recovery. Keep these
// helpers for drivers only; App Store application installs use the clean
// restart helpers below.
inline bool equalFile(const std::string& path, const char* expected, size_t length) {
  if (!expected) return false;
  HalFile file = Storage.open(path.c_str(), O_RDONLY);
  if (!file.isOpen() || file.isDirectory()) {
    if (file.isOpen()) (void)file.close();
    return false;
  }
  if (file.fileSize64() != length) { (void)file.close(); return false; }
  uint8_t bytes[256]{};
  size_t at = 0;
  bool matches = true;
  while (at < length) {
    const size_t n = length - at < sizeof(bytes) ? length - at : sizeof(bytes);
    if (file.read(bytes, n) != static_cast<int>(n) ||
        std::memcmp(bytes, expected + at, n)) { matches = false; break; }
    at += n;
  }
  return file.close() && matches;
}


inline bool stagePrefixMatches(const std::string& sourcePath,
                               const std::string& stagedPath, uint64_t limit) {
  HalFile source = Storage.open(sourcePath.c_str(), O_RDONLY);
  HalFile stage = Storage.open(stagedPath.c_str(), O_RDONLY);
  if (!source.isOpen() || !stage.isOpen() || source.isDirectory() || stage.isDirectory()) {
    if (source.isOpen()) (void)source.close();
    if (stage.isOpen()) (void)stage.close();
    return false;
  }
  const uint64_t length = stage.fileSize64();
  const uint64_t expected = source.fileSize64();
  // Metadata is bounded to 4096, not necessarily exactly 4096 bytes. Entry
  // lengths are independently checked by the full source verifier above.
  if (expected > limit || length > expected) {
    (void)source.close(); (void)stage.close();
    return false;
  }
  // This comparison runs under the nested online installer and package
  // verifier. Two local 512-byte arrays consumed a kilobyte of loopTask's
  // remaining stack. Allocate once and fail closed without touching the stage.
  std::unique_ptr<uint8_t[]> buffers(new (std::nothrow) uint8_t[1024]{});
  if (!buffers) {
    (void)source.close(); (void)stage.close();
    return false;
  }
  uint8_t* const lhs = buffers.get();
  uint8_t* const rhs = buffers.get() + 512;
  uint64_t at = 0;
  bool equal = true;
  while (at < length) {
    const size_t n = length - at < 512 ? static_cast<size_t>(length - at) : 512;
    if (source.read(lhs, n) != static_cast<int>(n) ||
        stage.read(rhs, n) != static_cast<int>(n) ||
        std::memcmp(lhs, rhs, n)) { equal = false; break; }
    at += n;
    RuntimePackages::ordinaryCooperativeYield(at, length);
  }
  const bool closeSource = source.close();
  const bool closeStage = stage.close();
  return equal && closeSource && closeStage;
}

// A reboot can leave /Apps/.<id>.pkg-stage after the ordinary installer wrote
// a prefix but before it sealed the manifest. Reclaim it ONLY when a freshly
// reverified, complete source proves every staged byte is an identical prefix
// of one declared file. A foreign file, mismatched bytes or different version
// remains untouched for inspection; no target/backup generation is touched.
inline bool discardMatchingStage(const std::string& sourceRoot,
    const RuntimePackages::OrdinaryPackagePlan& plan,
    const RuntimePackages::PackageRuntimePolicy& policy,
    uint32_t (*resolver)(const char*)) {
  using namespace RuntimePackages;
  OrdinaryTransactionPaths paths{};
  if (!ordinaryTransactionPaths(plan.identity.kind, plan.identity.id, paths)) return false;
  if (!Storage.exists(paths.stage)) return true;
  Identity verified{};
  if (!verifyOrdinarySdDirectory(sourceRoot.c_str(), policy, resolver, verified) ||
      !samePackage(plan.identity, verified) ||
      std::strcmp(plan.identity.version, verified.version) ||
      std::strcmp(plan.identity.artifact, verified.artifact)) return false;
  HalFile directory = Storage.open(paths.stage, O_RDONLY);
  if (!directory.isOpen() || !directory.isDirectory()) {
    if (directory.isOpen()) (void)directory.close();
    return false;
  }
  bool seen[kMaxPackageEntries]{}, manifestSeen = false, valid = true;
  for (;;) {
    HalFile entry = directory.openNextFile();
    if (!entry.isOpen()) break;
    char name[128]{};
    const size_t n = entry.getName(name, sizeof(name));
    const bool regular = n && n < sizeof(name) && !entry.isDirectory();
    (void)entry.close();
    if (!regular) { valid = false; break; }
    if (!std::strcmp(name, kOrdinaryManifestName)) {
      if (manifestSeen || !stagePrefixMatches(sourceRoot + "/" + name,
              std::string(paths.stage) + "/" + name, 4096)) { valid = false; break; }
      manifestSeen = true;
      continue;
    }
    size_t i = 0;
    while (i < plan.entryCount && std::strcmp(plan.entries[i].name, name)) ++i;
    if (i == plan.entryCount || seen[i] ||
        !stagePrefixMatches(sourceRoot + "/" + name,
            std::string(paths.stage) + "/" + name, plan.entries[i].sizeBytes)) {
      valid = false; break;
    }
    seen[i] = true;
  }
  const bool closed = directory.close();
  if (!valid || !closed) return false;
  for (size_t i = 0; i < plan.entryCount; ++i)
    if (seen[i] && !Storage.remove((std::string(paths.stage) + "/" + plan.entries[i].name).c_str()))
      return false;
  if (manifestSeen && !Storage.remove((std::string(paths.stage) + "/" + kOrdinaryManifestName).c_str()))
    return false;
  return Storage.rmdir(paths.stage);
}


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
