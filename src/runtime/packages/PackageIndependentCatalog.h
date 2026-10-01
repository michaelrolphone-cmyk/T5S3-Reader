#pragma once

#include "PackageCatalog.h"
#include "PackageRteZip.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace RuntimePackages {

// release-index.json is discovery metadata, never an execution grant. Keep
// historical loose records as version barriers; only explicit rte.zip records
// can become ordinary catalog candidates. The caller owns this bounded output
// (bounded below 300 KiB) and the immutable input, normally in PSRAM, not a task stack.
constexpr size_t kIndependentCatalogMaxBytes = 512u * 1024u;
constexpr size_t kIndependentCatalogMaxDrivers = 64;
constexpr size_t kIndependentCatalogMaxApps = 128;
constexpr size_t kIndependentCatalogMaxModulesPerKind = 64;
constexpr size_t kPackageReleaseTagBytes = 128;

struct IndependentDriverRecord {
  // The historical publisher admitted 64-byte IDs, dots and trailing marks.
  // Do not narrow those valid version barriers to today's ordinary identity.
  char id[65]{};
  char version[32]{};
  char tag[kPackageReleaseTagBytes]{};
  bool bundled = false;
  CatalogPackage package{};
};

using IndependentAppRecord = IndependentDriverRecord;

struct IndependentDriverCatalog {
  uint32_t schema = 0;
  IndependentDriverRecord rows[kIndependentCatalogMaxDrivers]{};
  size_t rowCount = 0;
  IndependentAppRecord appRows[kIndependentCatalogMaxApps]{};
  size_t appRowCount = 0;
  IndependentDriverRecord serviceRows[kIndependentCatalogMaxModulesPerKind]{};
  size_t serviceRowCount = 0;
  IndependentDriverRecord providerRows[kIndependentCatalogMaxModulesPerKind]{};
  size_t providerRowCount = 0;
  // Reused manifest workspace belongs to the heap/PSRAM catalog allocation.
  // A local plan here added ~4.8 KiB on top of the ordinary parser's frame,
  // risking exhaustion of the native invocation stack before HTTP caller/app frames.
  OrdinaryPackagePlan parsingScratch{};
};

static_assert(sizeof(IndependentDriverCatalog) < 300u * 1024u, "independent catalog allocation bound");

inline void clearIndependentDriverCatalog(IndependentDriverCatalog& out) {
  out.schema = 0;
  out.rowCount = 0;
  out.appRowCount = 0;
  out.serviceRowCount = out.providerRowCount = 0;
  clearOrdinaryManifestPlan(out.parsingScratch);
  for (size_t i = 0; i < kIndependentCatalogMaxApps; ++i)
    out.appRows[i] = IndependentAppRecord();
  for (size_t i = 0; i < kIndependentCatalogMaxDrivers; ++i)
    out.rows[i] = IndependentDriverRecord();
  for (size_t i = 0; i < kIndependentCatalogMaxModulesPerKind; ++i) {
    out.serviceRows[i] = IndependentDriverRecord();
    out.providerRows[i] = IndependentDriverRecord();
  }
}

namespace IndependentCatalogDetail {
inline const char* recordKind(Kind kind) {
  switch (kind) {
    case Kind::Application: return "app";
    case Kind::Driver: return "driver";
    case Kind::Service: return "service";
    case Kind::Provider: return "provider";
  }
  return "";
}
inline const char* packageKind(Kind kind) {
  return kind == Kind::Application ? "application" : recordKind(kind);
}


// The portable callback can enforce a caller deadline/cancellation and report
// throttled progress. No allocation or I/O occurs here. It runs at entry, at
// most 1024 consumed bytes apart (also within skipped metadata), per row, and
// at completion. Embedded callers must provide a callback that checks elapsed
// time/cancellation, enforces the operation deadline and actually yields.
// The two bounded passes intentionally first reject duplicate keys in *all*
// metadata, then decode the selected records; manifests get one bounded reparse.
class WorkBudget {
 public:
  WorkBudget(bool (*callback)(void*), void* context)
      : callback_(callback), context_(context) {}
  bool poll(bool force = false) {
    if (!alive_) return false;
    const bool due = force || bytes_ >= 1024;
    if (due) {
      bytes_ = 0;
      if (callback_ && !callback_(context_)) alive_ = false;
    }
    return alive_;
  }
  bool byte() {
    if (!alive_) return false;
    ++bytes_;
    return bytes_ < 1024 || poll();
  }
  bool alive() const { return alive_; }
 private:
  bool (*callback_)(void*);
  void* context_;
  size_t bytes_ = 0;
  bool alive_ = true;
};

// A bounded JSON cursor. Keys are unescaped ASCII and duplicate-checked;
// ignored descriptive values may contain valid JSON escapes and UTF-8.
// Identity-bearing values use text(), which admits only unescaped ASCII.
class Cursor {
 public:
  Cursor(const char* data, size_t length, WorkBudget& budget)
      : data_(data), length_(length), budget_(budget) {}
  void ws() {
    while (position_ < length_ && budget_.alive()) {
      const char c = data_[position_];
      if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
      advance();
    }
  }
  bool take(char c) {
    ws();
    if (!budget_.alive() || position_ == length_ || data_[position_] != c) return false;
    return advance();
  }
  bool next(char close, bool& more) {
    if (take(close)) { more = false; return true; }
    if (take(',')) { more = true; return true; }
    return false;
  }
  bool end() { ws(); return budget_.alive() && position_ == length_; }
  bool text(char* out, size_t capacity) {
    if (!out || capacity < 2 || !take('"')) return false;
    size_t used = 0;
    while (position_ < length_) {
      const unsigned char c = static_cast<unsigned char>(data_[position_]);
      if (!advance()) return false;
      if (c == '"') { out[used] = 0; return used != 0; }
      if (c < 0x20 || c > 0x7e || c == '\\' || used + 1 >= capacity) return false;
      out[used++] = static_cast<char>(c);
    }
    return false;
  }
  bool key(char* out, size_t capacity) { return text(out, capacity) && take(':'); }
  bool integer(uint64_t& value) {
    ws();
    if (position_ == length_ || data_[position_] < '0' || data_[position_] > '9') return false;
    if (data_[position_] == '0' && position_ + 1 < length_ &&
        data_[position_ + 1] >= '0' && data_[position_ + 1] <= '9') return false;
    value = 0;
    do {
      const uint64_t digit = static_cast<uint64_t>(data_[position_] - '0');
      if (value > (UINT64_MAX - digit) / 10u) return false;
      value = value * 10u + digit;
      if (!advance()) return false;
    } while (position_ < length_ && data_[position_] >= '0' && data_[position_] <= '9');
    return true;
  }
  bool nullValue() { ws(); return literal("null"); }
  bool objectAhead() { ws(); return position_ < length_ && data_[position_] == '{'; }
  bool slice(const char*& start, size_t& length) {
    ws();
    const size_t at = position_;
    if (!value()) return false;
    start = data_ + at;
    length = position_ - at;
    return true;
  }
  bool skip() { return value(); }
  bool objectOnly() { return objectAhead() && value() && end(); }
 private:
  struct Key { size_t offset; size_t length; };
  static constexpr size_t kMaxDepth = 10;
  static constexpr size_t kMaxKeys = 64;
  const char* data_;
  size_t length_;
  WorkBudget& budget_;
  size_t position_ = 0;
  size_t depth_ = 0;
  size_t activeKeys_ = 0;
  Key keys_[kMaxKeys]{};

  bool advance() { ++position_; return budget_.byte(); }
  bool rawTake(char c) {
    if (position_ == length_ || data_[position_] != c || !budget_.alive()) return false;
    return advance();
  }
  bool literal(const char* text) {
    const size_t n = std::strlen(text);
    if (n > length_ - position_ || std::memcmp(data_ + position_, text, n)) return false;
    for (size_t i = 0; i < n; ++i) if (!advance()) return false;
    return true;
  }
  bool hex4(uint16_t& result) {
    result = 0;
    for (unsigned i = 0; i < 4; ++i) {
      if (position_ == length_) return false;
      const char c = data_[position_];
      const int digit = c >= '0' && c <= '9' ? c - '0' :
                        c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                        c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
      if (digit < 0 || !advance()) return false;
      result = static_cast<uint16_t>((result << 4) | digit);
    }
    return true;
  }
  bool utf8(unsigned char first) {
    unsigned remaining = 0;
    uint32_t cp = 0, minimum = 0;
    if (first >= 0xc2 && first <= 0xdf) { remaining = 1; cp = first & 31u; minimum = 0x80; }
    else if (first >= 0xe0 && first <= 0xef) { remaining = 2; cp = first & 15u; minimum = 0x800; }
    else if (first >= 0xf0 && first <= 0xf4) { remaining = 3; cp = first & 7u; minimum = 0x10000; }
    else return false;
    for (unsigned i = 0; i < remaining; ++i) {
      if (position_ == length_) return false;
      const unsigned char next = static_cast<unsigned char>(data_[position_]);
      if (next < 0x80 || next > 0xbf || !advance()) return false;
      cp = (cp << 6) | (next & 63u);
    }
    return cp >= minimum && cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff);
  }
  bool string(bool key, size_t& offset, size_t& length) {
    if (!rawTake('"')) return false;
    offset = position_;
    while (position_ < length_) {
      const unsigned char c = static_cast<unsigned char>(data_[position_]);
      if (!advance()) return false;
      if (c == '"') {
        length = position_ - offset - 1;
        return !key || (length && length <= 63);
      }
      if (c < 0x20 || (key && c >= 0x7f)) return false;
      if (c >= 0x80) { if (!utf8(c)) return false; continue; }
      if (c != '\\') continue;
      if (key || position_ == length_) return false;
      const char escaped = data_[position_];
      if (!advance()) return false;
      if (escaped == '"' || escaped == '\\' || escaped == '/' || escaped == 'b' ||
          escaped == 'f' || escaped == 'n' || escaped == 'r' || escaped == 't') continue;
      uint16_t first = 0, second = 0;
      if (escaped != 'u' || !hex4(first) || (first >= 0xdc00 && first <= 0xdfff)) return false;
      if (first >= 0xd800 && first <= 0xdbff &&
          (!rawTake('\\') || !rawTake('u') || !hex4(second) ||
           second < 0xdc00 || second > 0xdfff)) return false;
    }
    return false;
  }
  bool digits() {
    const size_t start = position_;
    while (position_ < length_ && data_[position_] >= '0' && data_[position_] <= '9')
      if (!advance()) return false;
    return position_ != start;
  }
  bool number() {
    (void)rawTake('-');
    if (!rawTake('0')) {
      if (position_ == length_ || data_[position_] < '1' || data_[position_] > '9' || !digits()) return false;
    }
    if (rawTake('.') && !digits()) return false;
    if (position_ < length_ && (data_[position_] == 'e' || data_[position_] == 'E')) {
      if (!advance()) return false;
      if (position_ < length_ && (data_[position_] == '+' || data_[position_] == '-'))
        if (!advance()) return false;
      if (!digits()) return false;
    }
    return budget_.alive();
  }
  bool value() {
    ws();
    if (position_ == length_ || !budget_.alive()) return false;
    size_t offset = 0, length = 0;
    switch (data_[position_]) {
      case '{': return object();
      case '[': return array();
      case '"': return string(false, offset, length);
      case 't': return literal("true");
      case 'f': return literal("false");
      case 'n': return literal("null");
      default: return number();
    }
  }
  bool object() {
    if (depth_ >= kMaxDepth || !take('{')) return false;
    ++depth_;
    const size_t base = activeKeys_;
    if (take('}')) { --depth_; return true; }
    for (;;) {
      ws();
      size_t offset = 0, length = 0;
      if (!string(true, offset, length)) return false;
      for (size_t i = base; i < activeKeys_; ++i)
        if (keys_[i].length == length &&
            !std::memcmp(data_ + keys_[i].offset, data_ + offset, length)) return false;
      if (activeKeys_ == kMaxKeys) return false;
      keys_[activeKeys_++] = {offset, length};
      if (!take(':') || !value()) return false;
      if (take('}')) { activeKeys_ = base; --depth_; return true; }
      if (!take(',')) return false;
    }
  }
  bool array() {
    if (depth_ >= kMaxDepth || !take('[')) return false;
    ++depth_;
    if (take(']')) { --depth_; return true; }
    size_t count = 0;
    for (;;) {
      if (++count > kIndependentCatalogMaxApps || !value()) return false;
      if (take(']')) { --depth_; return true; }
      if (!take(',')) return false;
    }
  }
};

inline bool historicalId(const char* id) {
  const size_t length = std::strlen(id);
  if (!length || length > 64) return false;
  for (size_t i = 0; i < length; ++i) {
    const char c = id[i];
    const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (!alnum && (!i || (c != '.' && c != '_' && c != '-'))) return false;
  }
  // The derived historical asset also must not contain adjacent dots.
  return !std::strstr(id, "..");
}

inline bool legacyFiles(Cursor& r) {
  if (!r.take('[') || r.take(']')) return false;
  unsigned names = 0;
  size_t count = 0;
  for (;;) {
    if (++count > 4 || !r.take('{')) return false;
    unsigned seen = 0;
    char name[32]{}, sha[65]{};
    uint64_t size = 0;
    for (;;) {
      char key[64]{};
      if (!r.key(key, sizeof(key))) return false;
      if (!std::strcmp(key, "name")) { if (!r.text(name, sizeof(name))) return false; seen |= 1; }
      else if (!std::strcmp(key, "size_bytes")) { if (!r.integer(size) || !size) return false; seen |= 2; }
      else if (!std::strcmp(key, "sha256")) { if (!r.text(sha, sizeof(sha))) return false; seen |= 4; }
      else if (!r.skip()) return false;
      bool more = false;
      if (!r.next('}', more)) return false;
      if (!more) break;
    }
    if (seen != 7 || !CatalogDetail::lowerSha256(sha)) return false;
    const unsigned bit = !std::strcmp(name, ".package.json") ? 1u :
                         !std::strcmp(name, "driver.elf") ? 2u :
                         !std::strcmp(name, "provider-abi.v1") ? 4u :
                         !std::strcmp(name, "privileged-imports.v1") ? 8u : 0u;
    if (!bit || (names & bit)) return false;
    names |= bit;
    bool more = false;
    if (!r.next(']', more)) return false;
    if (!more) return names == 15;
  }
}

inline bool legacyManifest(const char* data, size_t length,
                           const IndependentDriverRecord& row, WorkBudget& budget) {
  // Historical descriptors may carry optional display/profile metadata. It is
  // structurally checked and bounded, but never interpreted as new ZIP input.
  if (length > 4096) return false;
  Cursor r(data, length, budget);
  if (!r.take('{')) return false;
  unsigned seen = 0;
  char id[65]{}, version[32]{}, capability[128]{};
  for (;;) {
    char key[64]{};
    if (!r.key(key, sizeof(key))) return false;
    if (!std::strcmp(key, "id")) { if (!r.text(id, sizeof(id))) return false; seen |= 1; }
    else if (!std::strcmp(key, "version")) { if (!r.text(version, sizeof(version))) return false; seen |= 2; }
    else if (!std::strcmp(key, "capability")) { if (!r.text(capability, sizeof(capability))) return false; seen |= 4; }
    else if (!std::strcmp(key, "api")) { uint64_t api = 0; if (!r.integer(api) || !api) return false; seen |= 8; }
    else if (!std::strcmp(key, "files")) { if (!legacyFiles(r)) return false; seen |= 16; }
    else if (!std::strcmp(key, "schema") || !std::strcmp(key, "kind") ||
             !std::strcmp(key, "architecture") || !std::strcmp(key, "artifact") ||
             !std::strcmp(key, "min_runtime_api") || !std::strcmp(key, "entries")) return false;
    else if (!r.skip()) return false;
    bool more = false;
    if (!r.next('}', more)) return false;
    if (!more) break;
  }
  return r.end() && seen == 31 && !std::strcmp(id, row.id) && !std::strcmp(version, row.version);
}

inline bool bundleManifest(const char* data, size_t length,
                           IndependentDriverRecord& row, WorkBudget& budget, OrdinaryPackagePlan& plan, Kind kind) {
  const bool app = kind == Kind::Application;
  if (!budget.poll(true)) return false;
  if (!parseOrdinaryManifest(data, length, plan) || plan.identity.kind != kind ||
      std::strcmp(plan.identity.id, row.id) || std::strcmp(plan.identity.version, row.version) ||
      std::strcmp(plan.architecture, row.package.architecture)) return false;
  uint64_t total = length;
  bool abi = false, imports = false;
  for (size_t i = 0; i < plan.entryCount; ++i) {
    const auto& entry = plan.entries[i];
    if (entry.sizeBytes > kRteZipMaxEntryBytes || total > kRteZipMaxTotalBytes - entry.sizeBytes) return false;
    total += entry.sizeBytes;
    abi = abi || !std::strcmp(entry.name, "provider-abi.v1");
    imports = imports || !std::strcmp(entry.name, "privileged-imports.v1");
  }
  if (!app && (!abi || !imports)) return false;
  if (app) {
    char sidecar[80]{}, executable[80]{};
    std::snprintf(sidecar, sizeof(sidecar), "%s.json", row.id);
    std::snprintf(executable, sizeof(executable), "%s.elf", row.id);
    bool found = false;
    for (size_t i = 0; i < plan.entryCount; ++i)
      found = found || (!std::strcmp(plan.entries[i].name, sidecar) && !plan.entries[i].executable);
    if (!found || std::strcmp(plan.identity.artifact, executable)) return false;
  }
  row.package.identity = plan.identity;
  return budget.poll(true);
}

inline bool appManifest(const char* data, size_t length,
                        const IndependentDriverRecord& row, WorkBudget& budget) {
  // Match the existing native application sidecar intake bound.
  if (length > 8192) return false;
  Cursor r(data, length, budget);
  if (!r.take('{')) return false;
  unsigned seen = 0;
  char filename[160]{}, version[32]{}, sha[65]{};
  uint64_t size = 0;
  for (;;) {
    char key[64]{};
    if (!r.key(key, sizeof(key))) return false;
    if (!std::strcmp(key, "file_name")) { if (!r.text(filename, sizeof(filename))) return false; seen |= 1; }
    else if (!std::strcmp(key, "version")) { if (!r.text(version, sizeof(version))) return false; seen |= 2; }
    else if (!std::strcmp(key, "size_bytes")) { if (!r.integer(size)) return false; seen |= 4; }
    else if (!std::strcmp(key, "sha256")) { if (!r.text(sha, sizeof(sha))) return false; seen |= 8; }
    else if (!r.skip()) return false;
    bool more = false;
    if (!r.next('}', more)) return false;
    if (!more) break;
  }
  // Pre-integrity app sidecars may omit these two fields. If present, they
  // must agree with the immutable release record rather than shadow it.
  return r.end() && (seen & 3u) == 3u && !std::strcmp(filename, row.package.archive) &&
         !std::strcmp(version, row.version) && (!(seen & 4u) || size == row.package.sizeBytes) &&
         (!(seen & 8u) || !std::strcmp(sha, row.package.sha256));
}

inline bool record(Cursor& r, IndependentDriverRecord& row, Kind packageType, WorkBudget& budget,
                   OrdinaryPackagePlan& scratch) {
  const bool app = packageType == Kind::Application;
  if (!recordKind(packageType)[0]) return false;
  if (!r.take('{')) return false;
  unsigned seen = 0;
  char kind[16]{}, format[16]{}, url[384]{}, sourceRepo[64]{};
  const char* manifest = nullptr;
  size_t manifestLength = 0;
  for (;;) {
    char key[64]{};
    if (!r.key(key, sizeof(key))) return false;
    if (!std::strcmp(key, "id")) { if (!r.text(row.id, sizeof(row.id))) return false; seen |= 1; }
    else if (!std::strcmp(key, "version")) { if (!r.text(row.version, sizeof(row.version))) return false; seen |= 2; }
    else if (!std::strcmp(key, "tag")) { if (!r.text(row.tag, sizeof(row.tag))) return false; seen |= 4; }
    else if (!std::strcmp(key, "asset")) { if (!r.text(row.package.archive, sizeof(row.package.archive))) return false; seen |= 8; }
    else if (!std::strcmp(key, "url")) { if (!r.text(url, sizeof(url))) return false; seen |= 16; }
    else if (!std::strcmp(key, "size")) { if (!r.integer(row.package.sizeBytes)) return false; seen |= 32; }
    else if (!std::strcmp(key, "sha256")) { if (!r.text(row.package.sha256, sizeof(row.package.sha256))) return false; seen |= 64; }
    else if (!std::strcmp(key, "manifest")) { if (!r.objectAhead() || !r.slice(manifest, manifestLength)) return false; seen |= 128; }
    else if (!std::strcmp(key, "kind")) { if (!r.text(kind, sizeof(kind)) || std::strcmp(kind, recordKind(packageType))) return false; }
    else if (!std::strcmp(key, "format")) {
      if (!r.text(format, sizeof(format)) || std::strcmp(format, "rte.zip")) return false;
      row.bundled = true;
    } else if (!std::strcmp(key, "architecture")) {
      if (!r.text(row.package.architecture, sizeof(row.package.architecture))) return false;
      seen |= 256;
    } else if (!std::strcmp(key, "source_repo")) {
      if (!app || !r.text(sourceRepo, sizeof(sourceRepo)) ||
          std::strcmp(sourceRepo, "michaelrolphone-cmyk/T5S3-GameBoy")) return false;
    } else if (!r.skip()) return false;
    bool more = false;
    if (!r.next('}', more)) return false;
    if (!more) break;
  }
  if ((seen & 255u) != 255u || !historicalId(row.id) ||
      !OrdinaryManifestDetail::canonicalVersion(row.version) || !row.package.sizeBytes ||
      !CatalogDetail::lowerSha256(row.package.sha256)) return false;
  char expectedTag[kPackageReleaseTagBytes]{};
  int n = 0;
  if (sourceRepo[0]) {
    if (row.bundled || std::strcmp(row.id, "gameboy") || row.tag[0] != 'v' ||
        !OrdinaryManifestDetail::canonicalVersion(row.tag + 1)) return false;
    std::strcpy(expectedTag, row.tag);
    n = static_cast<int>(std::strlen(expectedTag));
  } else n = std::snprintf(expectedTag, sizeof(expectedTag), "%s-%s-v%s",
                           recordKind(packageType), row.id, row.version);
  if (n <= 0 || static_cast<size_t>(n) >= sizeof(expectedTag) || std::strcmp(row.tag, expectedTag)) return false;
  char expectedAsset[160]{};
  if (row.bundled) {
    if (!(seen & 256u) || row.package.sizeBytes < kRteZipEocdBytes ||
        row.package.sizeBytes > kRteZipMaxTotalBytes + 65536u ||
        !bundleManifest(manifest, manifestLength, row, budget, scratch, packageType)) return false;
    n = std::snprintf(expectedAsset, sizeof(expectedAsset), "%s-%s-%s-%s.rte.zip",
                      packageKind(packageType), row.id, row.version, row.package.architecture);
  } else {
    if (packageType != Kind::Application && packageType != Kind::Driver) return false;
    if ((seen & 256u) || !(app ? appManifest(manifest, manifestLength, row, budget) :
                                   legacyManifest(manifest, manifestLength, row, budget))) return false;
    n = std::snprintf(expectedAsset, sizeof(expectedAsset), app ? "%s.elf" : "%s--driver.elf", row.id);
  }
  if (n <= 0 || static_cast<size_t>(n) >= sizeof(expectedAsset) || std::strcmp(expectedAsset, row.package.archive)) return false;
  char expectedUrl[384]{};
  n = std::snprintf(expectedUrl, sizeof(expectedUrl),
      "https://github.com/%s/releases/download/%s/%s",
      sourceRepo[0] ? sourceRepo : "michaelrolphone-cmyk/T5S3-Reader", row.tag, expectedAsset);
  if (n <= 0 || static_cast<size_t>(n) >= sizeof(expectedUrl) || std::strcmp(url, expectedUrl)) return false;
  if (!row.bundled) row.package = CatalogPackage();
  return true;
}

inline bool parse(const char* json, size_t length, IndependentDriverCatalog& out, WorkBudget& budget) {
  // Duplicate and escaped keys anywhere, even skipped apps/firmware/extension
  // metadata, invalidate the entire snapshot before any candidate is usable.
  { Cursor guard(json, length, budget); if (!guard.objectOnly()) return false; }
  Cursor r(json, length, budget);
  if (!r.take('{')) return false;
  unsigned seen = 0;
  for (;;) {
    char key[64]{};
    if (!r.key(key, sizeof(key))) return false;
    if (!std::strcmp(key, "schema")) {
      uint64_t schema = 0;
      if (!r.integer(schema) || schema != 1) return false;
      out.schema = 1; seen |= 1;
    } else if (!std::strcmp(key, "firmware")) {
      if (r.objectAhead()) { if (!r.skip()) return false; }
      else if (!r.nullValue()) return false;
      seen |= 2;
    } else if (!std::strcmp(key, "apps")) {
      if (!r.take('[')) return false;
      if (!r.take(']')) for (;;) {
        IndependentDriverRecord app;
        if (!budget.poll(true) || out.appRowCount >= kIndependentCatalogMaxApps ||
            !record(r, app, Kind::Application, budget, out.parsingScratch)) return false;
        for (size_t i = 0; i < out.appRowCount; ++i)
          if (!std::strcmp(out.appRows[i].id, app.id)) return false;
        out.appRows[out.appRowCount] = app;
        ++out.appRowCount;
        bool more = false;
        if (!r.next(']', more)) return false;
        if (!more) break;
      }
      seen |= 4;
    } else if (!std::strcmp(key, "drivers") || !std::strcmp(key, "services") ||
               !std::strcmp(key, "providers")) {
      const Kind kind = !std::strcmp(key, "drivers") ? Kind::Driver :
                        !std::strcmp(key, "services") ? Kind::Service : Kind::Provider;
      auto* rows = kind == Kind::Driver ? out.rows :
                   kind == Kind::Service ? out.serviceRows : out.providerRows;
      auto& count = kind == Kind::Driver ? out.rowCount :
                    kind == Kind::Service ? out.serviceRowCount : out.providerRowCount;
      if (!r.take('[')) return false;
      if (!r.take(']')) for (;;) {
        if (!budget.poll(true) || count >= kIndependentCatalogMaxModulesPerKind ||
            !record(r, rows[count], kind, budget, out.parsingScratch)) return false;
        for (size_t i = 0; i < count; ++i)
          if (!std::strcmp(rows[i].id, rows[count].id)) return false;
        ++count;
        bool more = false;
        if (!r.next(']', more)) return false;
        if (!more) break;
      }
      if (kind == Kind::Driver) seen |= 8; // New arrays are optional in historical indexes.
    } else return false;
    bool more = false;
    if (!r.next('}', more)) return false;
    if (!more) break;
  }
  return seen == 15 && r.end() && budget.poll(true);
}
} // namespace IndependentCatalogDetail

inline bool parseIndependentDriverCatalog(const char* json, size_t length,
    IndependentDriverCatalog& out, bool (*checkpoint)(void*) = nullptr,
    void* context = nullptr) {
  clearIndependentDriverCatalog(out);
  if (!json || length < 2 || length > kIndependentCatalogMaxBytes) return false;
  IndependentCatalogDetail::WorkBudget budget(checkpoint, context);
  if (!budget.poll(true) || !IndependentCatalogDetail::parse(json, length, out, budget)) {
    clearIndependentDriverCatalog(out);
    return false;
  }
  return true;
}

// Bounded single-row adapter for the legacy app reader: current ZIP rows are
// validated and skipped, never interpreted as loose ELF/sidecar assets.
inline bool parseIndependentAppRecord(const char* json, size_t length,
    IndependentAppRecord& out, OrdinaryPackagePlan& scratch) {
  out = {};
  if (!json || length < 2 || length > 8192) return false;
  IndependentCatalogDetail::WorkBudget budget(nullptr, nullptr);
  IndependentCatalogDetail::Cursor guard(json, length, budget);
  if (!guard.objectOnly()) return false;
  IndependentCatalogDetail::Cursor reader(json, length, budget);
  if (!IndependentCatalogDetail::record(reader, out, Kind::Application, budget, scratch) || !reader.end()) {
    out = {}; return false;
  }
  return true;
}

} // namespace RuntimePackages
