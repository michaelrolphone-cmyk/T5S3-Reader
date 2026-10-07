#pragma once
#include <cstddef>
#include <cstring>

namespace RuntimePackages {
constexpr size_t kPackageResourcePathBytes = 128;
constexpr size_t kPackageResourceDepth = 8;

// Canonical lowercase FAT-safe components. Resource paths never grant access
// to an arbitrary filesystem root; callers append them only to owned roots.
inline bool safePackageResourcePath(const char* path) {
  if (!path) return false;
  size_t component = 0, depth = 1, length = 0;
  char previous = 0;
  for (; length < kPackageResourcePathBytes && path[length]; ++length) {
    const char c = path[length];
    const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (c == '/') {
      if (!component || !((previous >= 'a' && previous <= 'z') ||
                           (previous >= '0' && previous <= '9')) ||
          ++depth > kPackageResourceDepth) return false;
      component = 0;
    } else {
      if (!alnum && (!component || (c != '.' && c != '-' && c != '_'))) return false;
      if (c == '.' && previous == '.') return false;
      ++component;
    }
    previous = c;
  }
  return length && length < kPackageResourcePathBytes && component &&
      ((previous >= 'a' && previous <= 'z') || (previous >= '0' && previous <= '9'));
}

inline bool packagePathIsParent(const char* parent, const char* child) {
  if (!parent || !child) return false;
  const size_t length = std::strlen(parent);
  return !std::strncmp(parent, child, length) && child[length] == '/';
}
} // namespace RuntimePackages
