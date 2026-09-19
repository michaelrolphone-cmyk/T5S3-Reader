#pragma once

#include "PackageOrdinaryManifest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace RuntimePackages {

// Generic four-kind discovery index. One archive asset per package. Not an
// authorization token, chipset selector, or usb-provider-catalog.json.
constexpr size_t kCatalogMaxPackages = 64;
constexpr const char* kCatalogName = "package-catalog.json";

struct CatalogPackage {
  Identity identity{};
  char architecture[32]{};
  char archive[160]{};
  uint64_t sizeBytes = 0;
  char sha256[65]{};
};

struct PackageCatalog {
  uint32_t schema = 0;
  char release[64]{};
  CatalogPackage packages[kCatalogMaxPackages]{};
  size_t packageCount = 0;
};

enum class CatalogResult : uint8_t {
  Ready, InvalidInput, InvalidJson, DuplicatePackage, LimitExceeded
};

inline void clearPackageCatalog(PackageCatalog& catalog) {
  catalog.schema = 0;
  catalog.release[0] = 0;
  catalog.packageCount = 0;
  for (size_t i = 0; i < kCatalogMaxPackages; ++i)
    catalog.packages[i] = CatalogPackage();
}

namespace CatalogDetail {
using Reader = OrdinaryManifestDetail::Reader;
inline bool kindName(Reader& r, Kind& out) {
  char text[16]{};
  if (!r.text(text, sizeof(text))) return false;
  if (!std::strcmp(text, "application")) { out = Kind::Application; return true; }
  if (!std::strcmp(text, "driver")) { out = Kind::Driver; return true; }
  if (!std::strcmp(text, "service")) { out = Kind::Service; return true; }
  if (!std::strcmp(text, "provider")) { out = Kind::Provider; return true; }
  return false;
}
inline bool archiveName(const char* name) {
  if (!name) return false;
  size_t size = 0;
  for (; name[size]; ++size) {
    const char ch = name[size];
    const bool alnum = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
    if (!alnum && ch != '-' && ch != '_' && ch != '.') return false;
    if (ch == '.' && size && name[size - 1] == '.') return false;
    if (size + 1 >= sizeof(CatalogPackage::archive)) return false;
  }
  return size > 8 && std::strcmp(name + size - 8, ".rte.zip") == 0;
}
} // namespace CatalogDetail

inline bool parsePackageCatalog(const char* json, size_t length,
                                PackageCatalog& out) {
  clearPackageCatalog(out);
  if (!safePackageJsonObject(json, length)) return false;
  CatalogDetail::Reader r(json, length);
  if (!r.take('{')) return false;
  unsigned seen = 0;
  bool valid = true;
  while (valid) {
    char key[24]{};
    if (!r.key(key, sizeof(key))) { valid = false; break; }
    if (!std::strcmp(key, "schema")) {
      uint64_t schema = 0;
      valid = !(seen & 1u) && r.integer(schema) && schema == 1u;
      out.schema = 1;
      seen |= 1u;
    } else if (!std::strcmp(key, "release")) {
      valid = !(seen & 2u) && r.text(out.release, sizeof(out.release));
      seen |= 2u;
    } else if (!std::strcmp(key, "packages")) {
      if (seen & 4u || !r.take('[')) { valid = false; break; }
      seen |= 4u;
      bool more = true;
      if (r.take(']')) more = false;
      while (valid && more) {
        if (out.packageCount >= kCatalogMaxPackages) {
          valid = false;
          break;
        }
        auto& pkg = out.packages[out.packageCount];
        if (!r.take('{')) { valid = false; break; }
        unsigned fields = 0;
        Kind kind{};
        char id[64]{}, version[32]{}, artifact[128]{};
        for (;;) {
          char field[24]{};
          if (!r.key(field, sizeof(field))) { valid = false; break; }
          if (!std::strcmp(field, "kind")) {
            valid = !(fields & 1u) && CatalogDetail::kindName(r, kind);
            fields |= 1u;
          } else if (!std::strcmp(field, "id")) {
            valid = !(fields & 2u) && r.text(id, sizeof(id));
            fields |= 2u;
          } else if (!std::strcmp(field, "version")) {
            valid = !(fields & 4u) && r.text(version, sizeof(version));
            fields |= 4u;
          } else if (!std::strcmp(field, "artifact")) {
            valid = !(fields & 8u) && r.text(artifact, sizeof(artifact));
            fields |= 8u;
          } else if (!std::strcmp(field, "architecture")) {
            valid = !(fields & 16u) &&
                    r.text(pkg.architecture, sizeof(pkg.architecture));
            fields |= 16u;
          } else if (!std::strcmp(field, "archive")) {
            valid = !(fields & 32u) && r.text(pkg.archive, sizeof(pkg.archive));
            fields |= 32u;
          } else if (!std::strcmp(field, "size_bytes")) {
            valid = !(fields & 64u) && r.integer(pkg.sizeBytes) && pkg.sizeBytes;
            fields |= 64u;
          } else if (!std::strcmp(field, "sha256")) {
            valid = !(fields & 128u) && r.text(pkg.sha256, sizeof(pkg.sha256));
            fields |= 128u;
          } else {
            valid = false;
          }
          if (!valid) break;
          bool next = false;
          if (!r.next('}', next)) { valid = false; break; }
          if (!next) break;
        }
        if (!valid || fields != 255u ||
            !makeIdentity(kind, id, version, artifact, false, &pkg.identity) ||
            !CatalogDetail::archiveName(pkg.archive) ||
            std::strlen(pkg.sha256) != 64)
          valid = false;
        if (!valid) break;
        for (size_t i = 0; i < out.packageCount; ++i) {
          if (out.packages[i].identity.kind == pkg.identity.kind &&
              !std::strcmp(out.packages[i].identity.id, pkg.identity.id)) {
            valid = false;
            break;
          }
        }
        if (!valid) break;
        ++out.packageCount;
        if (!r.next(']', more)) { valid = false; break; }
      }
    } else {
      valid = false;
    }
    if (!valid) break;
    bool more = false;
    if (!r.next('}', more)) { valid = false; break; }
    if (!more) break;
  }
  if (!valid || !r.end() || seen != 7u || !out.packageCount) {
    clearPackageCatalog(out);
    return false;
  }
  return true;
}

} // namespace RuntimePackages
