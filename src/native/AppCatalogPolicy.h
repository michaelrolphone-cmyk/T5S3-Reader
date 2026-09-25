#pragma once

#include <cstring>

// First-party apps removed from Apps/ may remain in already-published release
// indexes and historical release catalogs. Keep those stale entries out of the
// App Store until the indexes and releases expire.
namespace NativeAppCatalogPolicy {
inline bool isRetiredId(const char* id) {
  return id && std::strcmp(id, "hello") == 0;
}

inline bool isRetiredArtifact(const char* filename) {
  return filename && std::strcmp(filename, "hello.elf") == 0;
}
}  // namespace NativeAppCatalogPolicy
