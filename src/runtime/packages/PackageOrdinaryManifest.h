#pragma once

#include "PackageJsonGuard.h"
#include "PackageOrdinaryStage.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace RuntimePackages {

// RiscRTE ordinary manifest v1, retained verbatim as .package.json:
// {"schema":1,"kind":"driver","id":"example","version":"1.0.0",
//  "artifact":"driver.elf","architecture":"xtensa-esp32s3",
//  "min_runtime_api":1,
//  "entries":[{"name":"driver.elf","size_bytes":123,
//              "sha256":"<64 lowercase hex>","executable":true}],
//  "requires":[{"capability":"usb.host","min_api":1}]}
// All nine root fields are mandatory, including an empty requires array.
// This is a bounded ASCII-only canonical schema; aliases, escapes, unknown
// keys, duplicate keys, trailing text and implicit type conversion are refused.
// It never downloads, executes, activates, grants or authorizes an ELF.
// The caller must run preflightOrdinaryPackage with its REAL capability resolver
// and policy, independently reparse the retained manifest, enumerate exact
// files and rehash after staging/reboot before publication or loading.
namespace OrdinaryManifestDetail {
class Reader {
 public:
  Reader(const char* bytes, size_t length) : bytes_(bytes), length_(length) {}
  void whitespace() {
    while (pos_ < length_ && (bytes_[pos_] == ' ' || bytes_[pos_] == '\r' ||
                              bytes_[pos_] == '\t' || bytes_[pos_] == '\n')) ++pos_;
  }
  bool take(char token) {
    whitespace();
    if (pos_ >= length_ || bytes_[pos_] != token) return false;
    ++pos_;
    return true;
  }
  bool end() { whitespace(); return pos_ == length_; }
  bool word(const char* literal) {
    whitespace();
    const size_t count = std::strlen(literal);
    if (count > length_ - pos_ || std::memcmp(bytes_ + pos_, literal, count)) return false;
    pos_ += count;
    return true;
  }
  bool text(char* destination, size_t capacity) {
    if (!destination || capacity < 2 || !take('"')) return false;
    size_t used = 0;
    while (pos_ < length_) {
      const unsigned char byte = static_cast<unsigned char>(bytes_[pos_++]);
      if (byte == '"') {
        if (!used) return false;
        destination[used] = '\0';
        return true;
      }
      // All identifiers, architecture, names, digests and keys in this schema
      // are ASCII. Reject escape sequences rather than treating aliases as
      // distinct names on a case-insensitive/FAT SD implementation.
      if (byte < 0x20 || byte > 0x7e || byte == '\\' || used + 1 >= capacity)
        return false;
      destination[used++] = static_cast<char>(byte);
    }
    return false;
  }
  bool unsignedNumber(uint64_t& number) {
    whitespace();
    if (pos_ >= length_ || bytes_[pos_] < '0' || bytes_[pos_] > '9') return false;
    if (bytes_[pos_] == '0' && pos_ + 1 < length_ &&
        bytes_[pos_ + 1] >= '0' && bytes_[pos_ + 1] <= '9') return false;
    number = 0;
    do {
      const uint64_t digit = static_cast<uint64_t>(bytes_[pos_] - '0');
      if (number > (UINT64_MAX - digit) / 10u) return false;
      number = number * 10u + digit;
      ++pos_;
    } while (pos_ < length_ && bytes_[pos_] >= '0' && bytes_[pos_] <= '9');
    return true;
  }
  bool positive32(uint32_t& result) {
    uint64_t n = 0;
    if (!unsignedNumber(n) || !n || n > UINT32_MAX) return false;
    result = static_cast<uint32_t>(n);
    return true;
  }
  bool boolean(bool& value) {
    if (word("true")) { value = true; return true; }
    if (word("false")) { value = false; return true; }
    return false;
  }
  bool key(char* destination, size_t capacity) {
    return text(destination, capacity) && take(':');
  }
  bool more(bool& hasNext) {
    if (take('}')) { hasNext = false; return true; }
    if (take(',')) { hasNext = true; return true; }
    return false;
  }
  bool arrayMore(bool& hasNext) {
    if (take(']')) { hasNext = false; return true; }
    if (take(',')) { hasNext = true; return true; }
    return false;
  }
 private:
  const char* bytes_;
  size_t length_;
  size_t pos_ = 0;
};
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
      if (seen & 2u || !r.unsignedNumber(out.sizeBytes)) return false;
      seen |= 2u;
    } else if (!std::strcmp(key, "sha256")) {
      if (seen & 4u || !r.text(out.sha256, sizeof(out.sha256))) return false;
      seen |= 4u;
    } else if (!std::strcmp(key, "executable")) {
      if (seen & 8u || !r.boolean(out.executable)) return false;
      seen |= 8u;
    } else return false;
    bool next = false;
    if (!r.more(next)) return false;
    if (!next) return seen == 15u;
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
    bool next = false;
    if (!r.more(next)) return false;
    if (!next) return seen == 3u;
  }
}
inline bool entries(Reader& r, OrdinaryPackagePlan& plan) {
  if (!r.take('[') || r.take(']')) return false;
  for (;;) {
    if (plan.entryCount >= kMaxPackageEntries ||
        !entry(r, plan.entries[plan.entryCount])) return false;
    ++plan.entryCount;
    bool next = false;
    if (!r.arrayMore(next)) return false;
    if (!next) return true;
  }
}
inline bool requirements(Reader& r, OrdinaryPackagePlan& plan) {
  if (!r.take('[')) return false;
  if (r.take(']')) return true;
  for (;;) {
    if (plan.requirementCount >= kMaxPackageRequirements ||
        !requirement(r, plan.requirements[plan.requirementCount])) return false;
    ++plan.requirementCount;
    bool next = false;
    if (!r.arrayMore(next)) return false;
    if (!next) return true;
  }
}
} // namespace OrdinaryManifestDetail

inline bool parseOrdinaryManifest(const char* json, size_t length,
                                  OrdinaryPackagePlan& plan) {
  plan = {};
  if (!safePackageJsonObject(json, length)) return false;
  OrdinaryManifestDetail::Reader r(json, length);
  char kind[16]{}, id[64]{}, version[32]{}, artifact[128]{};
  uint32_t schema = 0;
  unsigned seen = 0;
  bool valid = r.take('{');
  while (valid) {
    char key[32]{};
    if (!r.key(key, sizeof(key))) { valid = false; break; }
    if (!std::strcmp(key, "schema")) {
      uint64_t n = 0;
      valid = !(seen & 1u) && r.unsignedNumber(n) && n == 1;
      schema = static_cast<uint32_t>(n);
      seen |= 1u;
    } else if (!std::strcmp(key, "kind")) {
      valid = !(seen & 2u) && r.text(kind, sizeof(kind));
      seen |= 2u;
    } else if (!std::strcmp(key, "id")) {
      valid = !(seen & 4u) && r.text(id, sizeof(id));
      seen |= 4u;
    } else if (!std::strcmp(key, "version")) {
      valid = !(seen & 8u) && r.text(version, sizeof(version));
      seen |= 8u;
    } else if (!std::strcmp(key, "artifact")) {
      valid = !(seen & 16u) && r.text(artifact, sizeof(artifact));
      seen |= 16u;
    } else if (!std::strcmp(key, "architecture")) {
      valid = !(seen & 32u) && r.text(plan.architecture, sizeof(plan.architecture));
      seen |= 32u;
    } else if (!std::strcmp(key, "min_runtime_api")) {
      valid = !(seen & 64u) && r.positive32(plan.minRuntimeApi);
      seen |= 64u;
    } else if (!std::strcmp(key, "entries")) {
      valid = !(seen & 128u) && OrdinaryManifestDetail::entries(r, plan);
      seen |= 128u;
    } else if (!std::strcmp(key, "requires")) {
      valid = !(seen & 256u) && OrdinaryManifestDetail::requirements(r, plan);
      seen |= 256u;
    } else valid = false;
    if (!valid) break;
    bool next = false;
    if (!r.more(next)) { valid = false; break; }
    if (!next) break;
  }
  if (!valid || !r.end() || seen != 511u || schema != 1u) { plan = {}; return false; }
  Kind parsed{};
  if (!std::strcmp(kind, "application")) parsed = Kind::Application;
  else if (!std::strcmp(kind, "driver")) parsed = Kind::Driver;
  else if (!std::strcmp(kind, "service")) parsed = Kind::Service;
  else if (!std::strcmp(kind, "provider")) parsed = Kind::Provider;
  else { plan = {}; return false; }
  if (!makeIdentity(parsed, id, version, artifact, false, &plan.identity) ||
      (std::strcmp(plan.architecture, "xtensa-esp32s3") &&
       std::strcmp(plan.architecture, "riscv32"))) { plan = {}; return false; }
  // Structural validation here is NOT dependency resolution. The actual
  // installation/recovery call MUST re-preflight against live capabilities.
  const PackageRuntimePolicy structural{plan.architecture,
      UINT32_MAX, 0, 1024u * 1024u, 4u * 1024u * 1024u};
  if (preflightOrdinaryPackage(plan, structural,
          [](const char*) -> uint32_t { return UINT32_MAX; }) !=
      PreflightResult::ReadyForContentVerification) { plan = {}; return false; }
  return true;
}

} // namespace RuntimePackages
