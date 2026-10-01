#pragma once
#include "PackageJsonGuard.h"
#include "PackageOrdinaryStage.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

namespace RuntimePackages {
// Bounded ASCII ordinary manifests remain .package.json. Schema 1 keeps flat
// resources; schema 2 permits bounded declared resource trees. ELF entry points
// remain canonical root basenames. Mandatory fields: schema=1|2, kind, id, version, artifact, architecture,
// min_runtime_api, entries[{name,size_bytes,sha256,executable}],
// requires[{capability,min_api}]. Empty requires is legal. Unknown fields,
// aliases, escapes, duplicate keys, implicit coercions and unbounded sizes
// fail closed. No digest or metadata grants permission to execute a driver.
namespace OrdinaryManifestDetail {
class Reader {
 public:
  Reader(const char* bytes, size_t length) : bytes_(bytes), length_(length) {}
  void ws() {
    while (pos_ < length_ && (bytes_[pos_] == ' ' || bytes_[pos_] == '\r' ||
                              bytes_[pos_] == '\n' || bytes_[pos_] == '\t')) ++pos_;
  }
  bool take(char token) {
    ws();
    if (pos_ >= length_ || bytes_[pos_] != token) return false;
    ++pos_;
    return true;
  }
  bool end() { ws(); return pos_ == length_; }
  bool word(const char* text) {
    ws();
    const size_t length = std::strlen(text);
    if (length > length_ - pos_ || std::memcmp(bytes_ + pos_, text, length)) return false;
    pos_ += length;
    return true;
  }
  bool text(char* out, size_t capacity) {
    if (!out || capacity < 2 || !take('"')) return false;
    size_t used = 0;
    while (pos_ < length_) {
      const unsigned char c = static_cast<unsigned char>(bytes_[pos_++]);
      if (c == '"') {
        if (!used) return false;
        out[used] = 0;
        return true;
      }
      if (c < 0x20 || c > 0x7e || c == '\\' || used + 1 >= capacity) return false;
      out[used++] = static_cast<char>(c);
    }
    return false;
  }
  bool integer(uint64_t& result) {
    ws();
    if (pos_ >= length_ || bytes_[pos_] < '0' || bytes_[pos_] > '9') return false;
    if (bytes_[pos_] == '0' && pos_ + 1 < length_ &&
        bytes_[pos_ + 1] >= '0' && bytes_[pos_ + 1] <= '9') return false;
    result = 0;
    do {
      const uint64_t digit = static_cast<uint64_t>(bytes_[pos_] - '0');
      if (result > (UINT64_MAX - digit) / 10u) return false;
      result = result * 10u + digit;
      ++pos_;
    } while (pos_ < length_ && bytes_[pos_] >= '0' && bytes_[pos_] <= '9');
    return true;
  }
  bool positive32(uint32_t& out) {
    uint64_t value = 0;
    if (!integer(value) || !value || value > UINT32_MAX) return false;
    out = static_cast<uint32_t>(value);
    return true;
  }
  bool boolean(bool& out) {
    if (word("true")) { out = true; return true; }
    if (word("false")) { out = false; return true; }
    return false;
  }
  bool key(char* out, size_t capacity) { return text(out, capacity) && take(':'); }
  bool next(char close, bool& more) {
    if (take(close)) { more = false; return true; }
    if (take(',')) { more = true; return true; }
    return false;
  }
 private:
  const char* bytes_;
  size_t length_;
  size_t pos_ = 0;
};
inline bool canonicalVersion(const char* version) {
  if (!safeVersion(version)) return false;
  const char* p = version;
  for (unsigned component = 0; component < 3; ++component) {
    if (*p == '0' && p[1] != '.' && p[1] != '\0') return false;
    while (*p >= '0' && *p <= '9') ++p;
    if (component < 2) {
      if (*p++ != '.') return false;
    }
  }
  return *p == '\0';
}
inline bool entry(Reader& r, OrdinaryEntry& out) {
  if (!r.take('{')) return false;
  unsigned seen = 0;
  for (;;) {
    char key[32]{};
    if (!r.key(key, sizeof(key))) return false;
    if (!std::strcmp(key, "name")) {
      if (seen & 1u || !r.text(out.name, sizeof(out.name))) return false;
      seen |= 1u;
    } else if (!std::strcmp(key, "size_bytes")) {
      if (seen & 2u || !r.integer(out.sizeBytes)) return false;
      seen |= 2u;
    } else if (!std::strcmp(key, "sha256")) {
      if (seen & 4u || !r.text(out.sha256, sizeof(out.sha256))) return false;
      seen |= 4u;
    } else if (!std::strcmp(key, "executable")) {
      if (seen & 8u || !r.boolean(out.executable)) return false;
      seen |= 8u;
    } else return false;
    bool more = false;
    if (!r.next('}', more)) return false;
    if (!more) return seen == 15u;
  }
}
inline bool requirement(Reader& r, OrdinaryRequirement& out) {
  if (!r.take('{')) return false;
  unsigned seen = 0;
  for (;;) {
    char key[32]{};
    if (!r.key(key, sizeof(key))) return false;
    if (!std::strcmp(key, "capability")) {
      if (seen & 1u || !r.text(out.capability, sizeof(out.capability))) return false;
      seen |= 1u;
    } else if (!std::strcmp(key, "min_api")) {
      if (seen & 2u || !r.positive32(out.minApi)) return false;
      seen |= 2u;
    } else return false;
    bool more = false;
    if (!r.next('}', more)) return false;
    if (!more) return seen == 3u;
  }
}
inline bool entries(Reader& r, OrdinaryPackagePlan& out) {
  if (!r.take('[') || r.take(']')) return false;
  for (;;) {
    if (out.entryCount == kMaxPackageEntries ||
        !entry(r, out.entries[out.entryCount])) return false;
    ++out.entryCount;
    bool more = false;
    if (!r.next(']', more)) return false;
    if (!more) return true;
  }
}
inline bool requirements(Reader& r, OrdinaryPackagePlan& out) {
  if (!r.take('[')) return false;
  if (r.take(']')) return true;
  for (;;) {
    if (out.requirementCount == kMaxPackageRequirements ||
        !requirement(r, out.requirements[out.requirementCount])) return false;
    ++out.requirementCount;
    bool more = false;
    if (!r.next(']', more)) return false;
    if (!more) return true;
  }
}
inline bool resourceImports(Reader& r, OrdinaryPackagePlan& out) {
  if (!r.take('[')) return false;
  if (r.take(']')) return true;
  for (;;) {
    if (out.resourceImportCount == kMaxResourceImports || !r.take('{')) return false;
    auto& value = out.resourceImports[out.resourceImportCount];
    unsigned seen = 0;
    for (;;) {
      char key[32]{};
      if (!r.key(key, sizeof(key))) return false;
      if (!std::strcmp(key, "id")) {
        if (seen & 1u || !r.text(value.id, sizeof(value.id)) || !safeId(value.id)) return false;
        seen |= 1u;
      } else if (!std::strcmp(key, "min_version")) {
        if (seen & 2u || !r.text(value.minVersion, sizeof(value.minVersion)) || !canonicalVersion(value.minVersion)) return false;
        seen |= 2u;
      } else return false;
      bool more = false;
      if (!r.next('}', more)) return false;
      if (!more) break;
    }
    if (seen != 3u) return false;
    for (size_t i = 0; i < out.resourceImportCount; ++i)
      if (!std::strcmp(out.resourceImports[i].id, value.id)) return false;
    ++out.resourceImportCount;
    bool more = false;
    if (!r.next(']', more)) return false;
    if (!more) return true;
  }
}
} // namespace OrdinaryManifestDetail

// Reinitialize in place. Aggregate assignment can materialize a second ~4.7 KiB
// plan on constrained caller stacks even when the destination lives in PSRAM.
inline void clearOrdinaryManifestPlan(OrdinaryPackagePlan& plan) {
  plan.~OrdinaryPackagePlan();
  ::new (static_cast<void*>(&plan)) OrdinaryPackagePlan{};
}

inline bool parseOrdinaryManifest(const char* json, size_t length,
                                 OrdinaryPackagePlan& plan) {
  clearOrdinaryManifestPlan(plan);
  if (!safePackageJsonObject(json, length)) return false;
  OrdinaryManifestDetail::Reader r(json, length);
  char kind[16]{}, id[64]{}, version[32]{}, artifact[128]{}, payload[16]{};
  bool artifactNull = false;
  uint64_t schema = 0;
  unsigned seen = 0;
  bool valid = r.take('{');
  while (valid) {
    char key[32]{};
    if (!r.key(key, sizeof(key))) { valid = false; break; }
    if (!std::strcmp(key, "schema")) {
      valid = !(seen & 1u) && r.integer(schema) && (schema == 1u || schema == 2u || schema == 3u);
      seen |= 1u;
    } else if (!std::strcmp(key, "kind")) {
      valid = !(seen & 2u) && r.text(kind, sizeof(kind)); seen |= 2u;
    } else if (!std::strcmp(key, "id")) {
      valid = !(seen & 4u) && r.text(id, sizeof(id)); seen |= 4u;
    } else if (!std::strcmp(key, "version")) {
      valid = !(seen & 8u) && r.text(version, sizeof(version)); seen |= 8u;
    } else if (!std::strcmp(key, "artifact")) {
      if (seen & 16u) valid = false;
      else if (r.word("null")) artifactNull = true;
      else valid = r.text(artifact, sizeof(artifact));
      seen |= 16u;
    } else if (!std::strcmp(key, "architecture")) {
      valid = !(seen & 32u) && r.text(plan.architecture, sizeof(plan.architecture)); seen |= 32u;
    } else if (!std::strcmp(key, "min_runtime_api")) {
      valid = !(seen & 64u) && r.positive32(plan.minRuntimeApi); seen |= 64u;
    } else if (!std::strcmp(key, "entries")) {
      valid = !(seen & 128u) && OrdinaryManifestDetail::entries(r, plan); seen |= 128u;
    } else if (!std::strcmp(key, "requires")) {
      valid = !(seen & 256u) && OrdinaryManifestDetail::requirements(r, plan); seen |= 256u;
    } else if (!std::strcmp(key, "payload")) {
      valid = !(seen & 512u) && r.text(payload, sizeof(payload)); seen |= 512u;
    } else if (!std::strcmp(key, "resource_imports")) {
      valid = !(seen & 1024u) && OrdinaryManifestDetail::resourceImports(r, plan); seen |= 1024u;
    } else valid = false;
    if (!valid) break;
    bool more = false;
    if (!r.next('}', more)) { valid = false; break; }
    if (!more) break;
  }
  if (!valid || !r.end() || seen != (schema == 3u ? 2047u : 511u) || (schema != 1u && schema != 2u && schema != 3u) ||
      !OrdinaryManifestDetail::canonicalVersion(version)) { clearOrdinaryManifestPlan(plan); return false; }
  plan.schemaVersion = static_cast<uint32_t>(schema);
  Kind parsed{};
  if (!std::strcmp(kind, "application")) parsed = Kind::Application;
  else if (!std::strcmp(kind, "driver")) parsed = Kind::Driver;
  else if (!std::strcmp(kind, "service")) parsed = Kind::Service;
  else if (!std::strcmp(kind, "provider")) parsed = Kind::Provider;
  else { clearOrdinaryManifestPlan(plan); return false; }
  const bool resources = schema == 3u && !std::strcmp(payload, "resources");
  const bool executable = schema != 3u || !std::strcmp(payload, "executable");
  const bool identityValid = resources ? artifactNull && makeResourceIdentity(parsed, id, version, &plan.identity) :
      executable && !artifactNull && makeIdentity(parsed, id, version, artifact, false, &plan.identity);
  if (!identityValid ||
      (std::strcmp(plan.architecture, "xtensa-esp32s3") &&
       std::strcmp(plan.architecture, "riscv32"))) { clearOrdinaryManifestPlan(plan); return false; }
  // Structural checks do not resolve grants or allow a driver to activate.
  // The real capability resolver and policy are mandatory at install/load time.
  const PackageRuntimePolicy structural{plan.architecture, UINT32_MAX,
      kOrdinaryMaxEntryBytes, kOrdinaryMaxTotalBytes};
  if (preflightOrdinaryPackage(plan, structural,
          [](const char*) -> uint32_t { return UINT32_MAX; }) !=
      PreflightResult::ReadyForContentVerification) { clearOrdinaryManifestPlan(plan); return false; }
  return true;
}
} // namespace RuntimePackages
