#pragma once

#include "PackageIndependentCatalog.h"
#include "PackagePreflight.h"
#include <string>

namespace RuntimePackages {

// A selection is a copied package plus its own immutable release. Aggregate
// schema 1 remains unchanged; independent records never rewrite its root tag.
constexpr size_t kOnlineReleaseTagBytes = kPackageReleaseTagBytes;
constexpr size_t kOnlineArchivePathBytes = 192;
struct OnlinePackageCatalog {
  CatalogPackage packages[kCatalogMaxPackages]{};
  char releases[kCatalogMaxPackages][kOnlineReleaseTagBytes]{};
  size_t packageCount = 0;
};

inline bool onlineArchiveUrl(const CatalogPackage& package, const char* release,
                             const char* architecture, std::string& url) {
  url.clear();
  if (!architecture || std::strcmp(package.architecture, architecture) ||
      !CatalogDetail::safeReleaseTag(release, kOnlineReleaseTagBytes) ||
      !CatalogDetail::archiveName(package.archive) ||
      !CatalogDetail::lowerSha256(package.sha256) ||
      package.sizeBytes < 22 || package.sizeBytes > 4u * 1024u * 1024u + 65536u)
    return false;
  url = std::string("https://github.com/michaelrolphone-cmyk/T5S3-Reader/releases/download/") +
        release + "/" + package.archive;
  return true;
}

inline bool sameCatalogPayload(const CatalogPackage& a, const CatalogPackage& b) {
  return samePackage(a.identity, b.identity) &&
      !std::strcmp(a.identity.version, b.identity.version) &&
      !std::strcmp(a.identity.artifact, b.identity.artifact) &&
      !std::strcmp(a.architecture, b.architecture) &&
      !std::strcmp(a.archive, b.archive) && a.sizeBytes == b.sizeBytes &&
      !std::strcmp(a.sha256, b.sha256);
}

// Inputs have already passed their source parsers. Independent legacy records
// are version barriers, never instructions to interpret a loose ELF as a ZIP.
// At most 64*(128+64) identity comparisons; no network, activation or filesystem I/O.
// Failure clears all selections, including partially merged rows.
inline bool mergeOnlineCatalog(const PackageCatalog* aggregate,
                               const IndependentDriverCatalog& independent,
                               const char* architecture, OnlinePackageCatalog& out) {
  out.packageCount = 0;
  auto fail = [&]() { out.packageCount = 0; return false; };
  if (!architecture || independent.schema != 1 ||
      independent.rowCount > kIndependentCatalogMaxDrivers ||
      independent.appRowCount > kIndependentCatalogMaxApps ||
      (aggregate && (aggregate->schema != 1 || aggregate->packageCount > kCatalogMaxPackages)))
    return fail();
  auto append = [&](const CatalogPackage& package, const char* tag) {
    std::string url;
    if (out.packageCount == kCatalogMaxPackages ||
        !onlineArchiveUrl(package, tag, architecture, url)) return false;
    out.packages[out.packageCount] = package;
    std::strcpy(out.releases[out.packageCount], tag);
    ++out.packageCount;
    return true;
  };
  if (aggregate) {
    for (size_t i = 0; i < aggregate->packageCount; ++i) {
      const auto& package = aggregate->packages[i];
      if (std::strcmp(package.architecture, architecture)) continue;
      bool retain = true;
      if (package.identity.kind == Kind::Driver) {
        for (size_t j = 0; j < independent.rowCount; ++j) {
          const auto& row = independent.rows[j];
          if (std::strcmp(package.identity.id, row.id)) continue;
          const auto order = comparePackageVersions(package.identity.version, row.version);
          if (order == VersionOrder::Invalid) return fail();
          if (order == VersionOrder::Equal && row.bundled &&
              !sameCatalogPayload(package, row.package)) return fail();
          retain = order == VersionOrder::Newer;
          break;
        }
      }
      if (package.identity.kind == Kind::Application) {
        for (size_t j = 0; j < independent.appRowCount; ++j) {
          const auto& row = independent.appRows[j];
          if (std::strcmp(package.identity.id, row.id)) continue;
          const auto order = comparePackageVersions(package.identity.version, row.version);
          if (order == VersionOrder::Invalid) return fail();
          retain = order == VersionOrder::Newer;
          break;
        }
      }
      if (retain && !append(package, aggregate->release)) return fail();
    }
  }
  for (size_t i = 0; i < independent.rowCount; ++i) {
    const auto& row = independent.rows[i];
    if (!row.bundled || std::strcmp(row.package.architecture, architecture)) continue;
    bool alreadyNewer = false;
    for (size_t j = 0; j < out.packageCount; ++j) {
      if (samePackage(out.packages[j].identity, row.package.identity)) {
        alreadyNewer = true;
        break;
      }
    }
    if (!alreadyNewer && !append(row.package, row.tag)) return fail();
  }
  return true;
}

} // namespace RuntimePackages
