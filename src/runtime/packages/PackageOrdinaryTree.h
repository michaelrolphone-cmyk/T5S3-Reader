#pragma once
#include "PackageOrdinaryStage.h"
#include "PackageVerificationReceipt.h"
#include <cstring>

namespace RuntimePackages {
// Store directory prefixes as references into the immutable plan, not a
// recursive filesystem walk or a 14 KiB path array. At most 16*7 parents.
struct OrdinaryTreeLayout {
  struct Directory { uint8_t entry; uint8_t length; };
  Directory directories[kMaxPackageEntries * (kPackageResourceDepth - 1)]{};
  size_t count = 0;
};
inline bool ordinaryTreeLayout(const OrdinaryPackagePlan& plan, OrdinaryTreeLayout& tree) {
  tree = {};
  if (plan.entryCount > kMaxPackageEntries) return false;
  for (size_t i = 0; i < plan.entryCount; ++i) {
    const char* name = plan.entries[i].name;
    if (!safePackageResourcePath(name)) return false;
    for (size_t n = 0; name[n]; ++n) {
      if (name[n] != '/') continue;
      bool present = false;
      for (size_t d = 0; d < tree.count; ++d) {
        const auto& old = tree.directories[d];
        if (old.length == n && !std::memcmp(plan.entries[old.entry].name, name, n)) present = true;
      }
      if (!present) {
        if (tree.count == sizeof(tree.directories) / sizeof(tree.directories[0])) return false;
        tree.directories[tree.count++] = {static_cast<uint8_t>(i), static_cast<uint8_t>(n)};
      }
    }
  }
  return true;
}
inline void ordinaryTreeDirectory(const OrdinaryPackagePlan& plan,
    const OrdinaryTreeLayout::Directory& directory, char (&out)[128]) {
  std::memcpy(out, plan.entries[directory.entry].name, directory.length);
  out[directory.length] = 0;
}

// Ops paths are relative to a separately validated, manager-owned root.
// visit(directory, callback) MUST distinguish read failure from clean EOF,
// bound enumeration and yield/check deadlines. No unknown directory is entered.
template<class Ops>
bool ordinaryTreeInventory(const OrdinaryPackagePlan& plan, Ops& ops, bool full, bool managedMetadata = false) {
  OrdinaryTreeLayout tree{};
  if (!ordinaryTreeLayout(plan, tree)) return false;
  bool files[kMaxPackageEntries]{}, directories[kMaxPackageEntries * (kPackageResourceDepth - 1)]{};
  bool manifest = false, receipt = false;
  size_t items = 0;
  for (size_t scan = 0; scan <= tree.count; ++scan) {
    char parent[128]{};
    if (scan) ordinaryTreeDirectory(plan, tree.directories[scan - 1], parent);
    if (scan && !ops.exists(parent)) { if (full) return false; else continue; }
    if (!ops.visit(parent, [&](const char* basename, bool isDirectory) {
      if (++items > plan.entryCount + tree.count + (managedMetadata ? 2u : 1u) || !basename ||
          std::strchr(basename, '/')) return false;
      char path[128]{};
      const size_t p = std::strlen(parent), n = std::strlen(basename);
      if (!n || p + (p ? 1 : 0) + n >= sizeof(path)) return false;
      if (p) { std::memcpy(path, parent, p); path[p] = '/'; }
      std::memcpy(path + p + (p ? 1 : 0), basename, n + 1);
      if (!std::strcmp(path, kOrdinaryManifestName)) {
        if (isDirectory || manifest) return false;
        manifest = true;
        return true;
      }
      if (managedMetadata && !std::strcmp(path, kPackageReceiptName)) {
        if (isDirectory || receipt) return false;
        receipt = true;
        return true;
      }
      if (!safePackageResourcePath(path)) return false;
      if (isDirectory) {
        for (size_t d = 0; d < tree.count; ++d) {
          const auto& directory = tree.directories[d];
          if (directory.length != std::strlen(path) ||
              std::memcmp(plan.entries[directory.entry].name, path, directory.length)) continue;
          if (directories[d]) return false;
          directories[d] = true;
          return true;
        }
      } else {
        for (size_t f = 0; f < plan.entryCount; ++f) {
          if (std::strcmp(plan.entries[f].name, path)) continue;
          if (files[f]) return false;
          files[f] = true;
          return true;
        }
      }
      return false;
    })) return false;
  }
  if (!full) return true;
  if (!manifest) return false;
  for (size_t f = 0; f < plan.entryCount; ++f) if (!files[f]) return false;
  for (size_t d = 0; d < tree.count; ++d) if (!directories[d]) return false;
  return true;
}

template<class Ops>
bool purgeOrdinaryTree(const OrdinaryPackagePlan& plan, Ops& ops, bool ownedPartial, bool managedMetadata = false) {
  if (!ordinaryTreeInventory(plan, ops, false, managedMetadata)) return false;
  const bool manifest = ops.exists(kOrdinaryManifestName);
  const bool receipt = managedMetadata && ops.exists(kPackageReceiptName);
  if (!manifest && !ownedPartial && (plan.entryCount || receipt)) return false;
  OrdinaryTreeLayout tree{};
  if (!ordinaryTreeLayout(plan, tree)) return false;
  // A known regular-file receipt is manager metadata, even when truncated.
  // It never supplies deletion authority; the retained manifest/owned plan does.
  if (receipt && !ops.remove(kPackageReceiptName)) return false;
  for (size_t f = 0; f < plan.entryCount; ++f)
    if (ops.exists(plan.entries[f].name) && !ops.remove(plan.entries[f].name)) return false;
  // Reverse lexical depth is sufficient; peers may be removed in any order.
  for (size_t remaining = tree.count; remaining; --remaining) {
    size_t largest = 0;
    for (size_t d = 1; d < remaining; ++d)
      if (tree.directories[d].length > tree.directories[largest].length) largest = d;
    char path[128]{};
    ordinaryTreeDirectory(plan, tree.directories[largest], path);
    if (ops.exists(path) && !ops.rmdir(path)) return false;
    tree.directories[largest] = tree.directories[remaining - 1];
  }
  // Metadata survives interrupted cleanup so restart can identify every leaf.
  if (manifest && !ops.remove(kOrdinaryManifestName)) return false;
  return ops.rmdir("");
}
} // namespace RuntimePackages
