#pragma once
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <limits>
namespace BootstrapPathPolicy {
constexpr size_t kMaxPath = 255;
inline bool valid(const char* p) {
  if (!p || *p != '/' || strnlen(p, kMaxPath + 1) > kMaxPath) return false;
  const char* part = p + 1;
  for (const char* c = part;; ++c) {
    if (*c == ':' || *c == '\\') return false;
    if (*c == '/' || !*c) {
      const size_t n = c - part;
      if ((n == 1 && part[0] == '.') || (n == 2 && part[0] == '.' && part[1] == '.')) return false;
      if (!*c) return true;
      part = c + 1;
    }
  }
}
// Choose creation semantics without converting an exclusive create into a
// truncate when both flags are present. FsApi flags are decoded by the caller.
enum class Create { Existing, NewOnly, Replace, OpenOrCreate };
constexpr Create creation(bool create, bool exclusive, bool truncate) {
  return create ? exclusive ? Create::NewOnly : truncate ? Create::Replace : Create::OpenOrCreate
                : truncate ? Create::Replace : Create::Existing;
}
inline bool relativeOffset(uint64_t position, int64_t delta, uint64_t& result) {
  if (position > static_cast<uint64_t>(INT64_MAX)) return false;
  const auto at = static_cast<int64_t>(position);
  if (delta < -at || delta > INT64_MAX - at) return false;
  result = static_cast<uint64_t>(at + delta);
  return true;
}
}
