#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace RuntimePackages {

// Distribution identity is independent of the physical ELF filename. A type
// and ID identify a package; its version identifies a candidate update. None
// of these fields confer trust or authorize the ELF to execute.
enum class Kind : uint8_t { Application, Driver, Service, Provider };

struct Identity {
  Kind kind = Kind::Application;
  char id[64]{};
  char version[32]{};
  char artifact[128]{};
  bool legacyVersion = false;
};

inline bool safeId(const char* id) {
  if (!id) return false;
  size_t size = 0;
  for (; size < sizeof(Identity::id) && id[size]; ++size) {
    const char ch = id[size];
    const bool alnum = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
    if (!alnum && (size == 0 || (ch != '-' && ch != '_'))) return false;
  }
  if (!size || size >= sizeof(Identity::id)) return false;
  const char last = id[size - 1];
  return (last >= 'a' && last <= 'z') || (last >= '0' && last <= '9');
}

// This is a single executable basename, not a path or URI. In particular,
// reject traversal, directory separators, drive separators and hidden files.
inline bool safeArtifact(const char* name) {
  if (!name) return false;
  size_t size = 0;
  for (; size < sizeof(Identity::artifact) && name[size]; ++size) {
    const char ch = name[size];
    const bool alnum = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                       (ch >= '0' && ch <= '9');
    if (!alnum && (size == 0 || (ch != '_' && ch != '-' && ch != '.'))) return false;
    if (ch == '.' && size && name[size - 1] == '.') return false;
  }
  return size > 4 && size < sizeof(Identity::artifact) &&
         std::strcmp(name + size - 4, ".elf") == 0;
}

// Numeric major.minor.patch, without prerelease or build metadata. Every
// component fits uint32_t so comparisons cannot overflow or truncate.
inline bool safeVersion(const char* version) {
  if (!version) return false;
  unsigned components = 0;
  size_t digits = 0;
  size_t size = 0;
  uint32_t component = 0;
  for (; size < sizeof(Identity::version) && version[size]; ++size) {
    const char ch = version[size];
    if (ch >= '0' && ch <= '9') {
      const uint32_t digit = static_cast<uint32_t>(ch - '0');
      if (component > (std::numeric_limits<uint32_t>::max() - digit) / 10) return false;
      component = component * 10 + digit;
      ++digits;
    } else if (ch == '.' && digits && components < 2) {
      ++components;
      digits = 0;
      component = 0;
    } else {
      return false;
    }
  }
  return size != 0 && size < sizeof(Identity::version) && components == 2 && digits != 0;
}

inline bool makeIdentity(Kind kind, const char* declaredId, const char* version,
                         const char* artifact, bool allowLegacyVersion, Identity* out) {
  if (out) *out = {};
  if (!out || !safeArtifact(artifact)) return false;
  switch (kind) {
    case Kind::Application: case Kind::Driver: case Kind::Service: case Kind::Provider: break;
    default: return false;
  }
  char inferred[sizeof(Identity::id)]{};
  const char* id = declaredId;
  if (!id || !id[0]) {
    if (kind != Kind::Application || (declaredId && !declaredId[0])) return false;
    const size_t length = std::strlen(artifact) - 4;
    if (!length || length >= sizeof(inferred)) return false;
    std::memcpy(inferred, artifact, length);
    id = inferred;
  }
  if (!safeId(id)) return false;
  const bool legacy = !version || !version[0];
  if ((legacy && !allowLegacyVersion) || (!legacy && !safeVersion(version))) return false;
  out->kind = kind;
  std::strcpy(out->id, id);
  if (!legacy) std::strcpy(out->version, version);
  std::strcpy(out->artifact, artifact);
  out->legacyVersion = legacy;
  return true;
}

inline bool samePackage(const Identity& a, const Identity& b) {
  return a.kind == b.kind && std::strcmp(a.id, b.id) == 0;
}

}  // namespace RuntimePackages
