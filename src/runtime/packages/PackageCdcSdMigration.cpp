#include "PackageCdcSdMigration.h"

#include <HalStorage.h>
#include <mbedtls/sha256.h>

#include <cstdio>
#include <memory>
#include <new>
#include <string>

#include "PackageOrdinarySdAdapter.h"
namespace RuntimePackages {
namespace {
constexpr size_t kIntentBytes = 256;
struct Ops {
  struct Special {
    const char* path;
    bool directory;
  };
  inline static constexpr Special special[] = {{kCdcCanonicalRoot, true},
                                               {kCdcAliasRoot, true},
                                               {kCdcHoldingRoot, true},
                                               {kCdcIntentPath, false},
                                               {kCdcIntentPart, false},
                                               {"/Drivers/.usb-cdc-acm.pkg-stage", true},
                                               {"/Drivers/.usb-cdc-acm.pkg-previous", true},
                                               {"/Drivers/.usb-cdc-acm.pkg-removing", true},
                                               {"/Drivers/.usb-cdc-acm-v2.pkg-stage", true},
                                               {"/Drivers/.usb-cdc-acm-v2.pkg-previous", true},
                                               {"/Drivers/.usb-cdc-acm-v2.pkg-removing", true},
                                               {"/Drivers/.usb-cdc-acm.install", true},
                                               {"/Drivers/.usb-cdc-acm.previous", true},
                                               {"/Drivers/.usb-cdc-acm-v2.install", true},
                                               {"/Drivers/.usb-cdc-acm-v2.previous", true}};
  mutable bool present[sizeof(special) / sizeof(special[0])]{};
  mutable bool healthy = false;
  Ops() { refresh(); }
  static bool foldedEqual(const char* a, const char* b) {
    while (*a && *b) {
      const char x = *a >= 'A' && *a <= 'Z' ? *a + ('a' - 'A') : *a;
      const char y = *b >= 'A' && *b <= 'Z' ? *b + ('a' - 'A') : *b;
      if (x != y) return false;
      ++a;
      ++b;
    }
    return !*a && !*b;
  }
  bool scan(HalFile& directory, bool parent) const {
    bool good = true;
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
    const TickType_t started = xTaskGetTickCount();
#endif
    for (size_t visited = 0; good; ++visited) {
      HalFile file = directory.openNextFile();
      if (!file.isOpen()) {
        good = directory.getError() == 0;
        break;
      }
      char name[128]{};
      const size_t length = file.getName(name, sizeof(name));
      if (visited == 256 || !length || length >= sizeof(name))
        good = false;
      else if (parent) {
        // A failed open of an existing Drivers root is uncertainty, not absence.
        if (foldedEqual(name, "Drivers")) good = false;
      } else {
        for (size_t i = 0; i < sizeof(special) / sizeof(special[0]); ++i) {
          const char* expected = std::strrchr(special[i].path, '/') + 1;
          if (!foldedEqual(name, expected)) continue;
          if (std::strcmp(name, expected) || present[i] || file.isDirectory() != special[i].directory) good = false;
          present[i] = true;
        }
      }
      if (!file.close()) good = false;
      ordinaryCooperativeYield(1, 1);  // Bounded item and real scheduler checkpoint.
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
      if (xTaskGetTickCount() - started >= pdMS_TO_TICKS(2000)) good = false;
#endif
    }
    const bool closed = directory.close();
    return good && closed;
  }
  void refresh() const {
    std::memset(present, 0, sizeof(present));
    healthy = false;
    if (!Storage.ready()) return;
    HalFile directory = Storage.open("/Drivers", O_RDONLY);
    if (directory.isOpen()) {
      if (!directory.isDirectory()) {
        (void)directory.close();
        return;
      }
      healthy = scan(directory, false);
      return;
    }
    // Fresh media may legitimately have no Drivers directory. Prove absence
    // from a checked parent listing rather than treating exists()==false as proof.
    HalFile parent = Storage.open("/", O_RDONLY);
    if (!parent.isOpen() || !parent.isDirectory()) {
      if (parent.isOpen()) (void)parent.close();
      return;
    }
    healthy = scan(parent, true);
  }
  bool exists(const char* path) const {
    for (size_t i = 0; i < sizeof(special) / sizeof(special[0]); ++i)
      if (!std::strcmp(path, special[i].path)) return !healthy || present[i];
    return Storage.exists(path);
  }
  bool rename(const char* from, const char* to) const {
    const bool renamed = Storage.rename(from, to);
    refresh();
    return renamed;
  }
  bool legacyPending() const {
    for (const char* id : {kCdcCanonicalId, kCdcAliasId}) {
      const std::string base = std::string("/Drivers/.") + id;
      if (exists((base + ".install").c_str()) || exists((base + ".previous").c_str())) return true;
    }
    return false;
  }
  bool read(const char* path, char* out, size_t capacity, size_t& used) const {
    used = 0;
    HalFile file = Storage.open(path, O_RDONLY);
    if (!file.isOpen() || file.isDirectory()) {
      if (file.isOpen()) (void)file.close();
      return false;
    }
    const uint64_t size = file.fileSize64();
    const bool good = size && size < capacity && file.read(out, static_cast<size_t>(size)) == static_cast<int>(size);
    const bool closed = file.close();
    if (!good || !closed) return false;
    used = static_cast<size_t>(size);
    out[used] = 0;
    return true;
  }
  bool manifestDigest(const char* root, uint8_t (&digest)[32]) const {
    std::unique_ptr<char[]> data(new (std::nothrow) char[4097]{});
    size_t used = 0;
    if (!data || !read((std::string(root) + "/.package.json").c_str(), data.get(), 4097, used)) return false;
    return mbedtls_sha256_ret(reinterpret_cast<const unsigned char*>(data.get()), used, digest, 0) == 0;
  }
  static size_t encode(const CdcMigrationIntent& intent, char (&out)[kIntentBytes]) {
    if (!CdcMigrationDetail::valid(intent)) return 0;
    char hex[2][65]{};
    constexpr char digits[] = "0123456789abcdef";
    const uint8_t* digests[] = {intent.manifestDigest, intent.aliasManifestDigest};
    for (size_t d = 0; d < 2; ++d)
      for (size_t i = 0; i < 32; ++i) {
        hex[d][i * 2] = digits[digests[d][i] >> 4];
        hex[d][i * 2 + 1] = digits[digests[d][i] & 15];
      }
    const int size = std::snprintf(out, sizeof(out), "RISC-CDC-1\n%s\n%s\n%s\n%s\n", intent.candidateVersion,
                                   intent.aliasVersion, hex[0], hex[1]);
    return size > 0 && static_cast<size_t>(size) < sizeof(out) ? static_cast<size_t>(size) : 0;
  }
  bool readIntent(CdcMigrationIntent& intent) const {
    intent = {};
    if (exists(kCdcIntentPath) && exists(kCdcIntentPart)) return false;
    const char* path = exists(kCdcIntentPath) ? kCdcIntentPath : kCdcIntentPart;
    char bytes[kIntentBytes]{};
    size_t used = 0;
    if (!read(path, bytes, sizeof(bytes), used) || used < 11 || std::strlen(bytes) != used ||
        std::memcmp(bytes, "RISC-CDC-1\n", 11))
      return false;
    char* next = bytes + 11;
    for (char* field : {intent.candidateVersion, intent.aliasVersion}) {
      char* end = std::strchr(next, '\n');
      if (!end || end == next || end - next >= 32) return false;
      std::memcpy(field, next, static_cast<size_t>(end - next));
      next = end + 1;
    }
    if (static_cast<size_t>(bytes + used - next) != 130) return false;
    for (uint8_t* digest : {intent.manifestDigest, intent.aliasManifestDigest}) {
      if (next[64] != '\n') return false;
      for (size_t i = 0; i < 64; ++i) {
        const char ch = next[i];
        const int v = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;
        if (v < 0) return false;
        digest[i / 2] = static_cast<uint8_t>((digest[i / 2] << 4) | v);
      }
      next += 65;
    }
    return CdcMigrationDetail::valid(intent);
  }
  bool writeIntent(const CdcMigrationIntent& intent) const {
    char bytes[kIntentBytes]{};
    const size_t size = encode(intent, bytes);
    if (!size || exists(kCdcIntentPath) || exists(kCdcIntentPart)) return false;
    HalFile file = Storage.open(kCdcIntentPart, O_WRONLY | O_CREAT | O_EXCL);
    if (!file.isOpen() || file.isDirectory()) {
      if (file.isOpen()) (void)file.close();
      return false;
    }
    const bool wrote = file.write(bytes, size) == size;
    const bool closed = file.close();
    CdcMigrationIntent check{};
    if (!wrote || !closed || !readIntent(check) || std::memcmp(&check, &intent, sizeof(intent))) {
      // O_EXCL established ownership; no directory has moved yet. Only a
      // definitely closed, newly created part may be removed on this path.
      if (closed) {
        (void)Storage.remove(kCdcIntentPart);
        refresh();
      }
      return false;
    }
    return rename(kCdcIntentPart, kCdcIntentPath);
  }
  bool purgeHolding(const CdcMigrationIntent& intent) const {
    const std::string manifest = std::string(kCdcHoldingRoot) + "/.package.json";
    if (exists(manifest.c_str())) {
      uint8_t digest[32]{};
      if (!manifestDigest(kCdcHoldingRoot, digest) || std::memcmp(digest, intent.aliasManifestDigest, sizeof(digest)))
        return false;
    }
    if (!exists(manifest.c_str()) && exists((std::string(kCdcHoldingRoot) + "/manifest.json").c_str())) return false;
    // No manifest is allowed only when manifest-last cleanup left an empty
    // directory. The shared purger rejects every remaining unknown entry.
    const bool purged = purgeManagedOrdinarySdDirectory(kCdcHoldingRoot, Kind::Driver, kCdcAliasId);
    refresh();
    return purged;
  }
  bool removeMatchingPart(const CdcMigrationIntent& intent) const {
    char expected[kIntentBytes]{};
    const size_t expectedSize = encode(intent, expected);
    HalFile part = Storage.open(kCdcIntentPart, O_RDONLY);
    if (!expectedSize || !part.isOpen() || part.isDirectory()) {
      if (part.isOpen()) (void)part.close();
      return false;
    }
    const uint64_t size = part.fileSize64();
    char actual[kIntentBytes]{};
    const bool matches = size <= expectedSize &&
                         (!size || part.read(actual, static_cast<size_t>(size)) == static_cast<int>(size)) &&
                         !std::memcmp(actual, expected, static_cast<size_t>(size));
    const bool closed = part.close();
    if (!matches || !closed) return false;
    const bool removed = Storage.remove(kCdcIntentPart);
    refresh();
    return removed;
  }
  bool clearIntent(const CdcMigrationIntent& expected) const {
    CdcMigrationIntent check{};
    if (!readIntent(check) || std::memcmp(&check, &expected, sizeof(expected))) return false;
    const bool removed = Storage.remove(exists(kCdcIntentPath) ? kCdcIntentPath : kCdcIntentPart);
    refresh();
    return removed;
  }
};
struct Verify {
  const PackageRuntimePolicy& policy;
  uint32_t (*resolver)(const char*);
  bool operator()(const char* path, Identity& observed) const {
    const char* id =
        (!std::strcmp(path, kCdcHoldingRoot) || !std::strcmp(path, kCdcAliasRoot)) ? kCdcAliasId : kCdcCanonicalId;
    return verifyManagedOrdinarySdDirectory(path, Kind::Driver, id, policy, resolver, observed);
  }
};
struct Purge {
  Ops* ops;
  bool operator()(const char* path) const {
    const bool purged = purgeManagedOrdinarySdDirectory(
        path, Kind::Driver, !std::strcmp(path, kCdcHoldingRoot) ? kCdcAliasId : kCdcCanonicalId);
    ops->refresh();
    return purged;
  }
};
// An interrupted precommit part is removable only as an exact bounded prefix
// reconstructed from both independently verified generations. Unknown records
// stay untouched. Both root leases and the shared manager gate are held.
bool cleanPrecommitPart(Ops& ops, Verify verify) {
  OrdinaryTransactionPaths canonical{}, alias{};
  ordinaryTransactionPaths(Kind::Driver, kCdcCanonicalId, canonical);
  ordinaryTransactionPaths(Kind::Driver, kCdcAliasId, alias);
  if (!ops.exists(kCdcIntentPart) || ops.exists(kCdcIntentPath) || ops.exists(kCdcHoldingRoot) ||
      ops.exists(canonical.backup) || ops.exists(canonical.removing) || ops.exists(alias.stage) ||
      ops.exists(alias.backup) || ops.exists(alias.removing) || ops.legacyPending())
    return false;
  Identity candidate{}, retired{};
  if (!OrdinaryTransactionDetail::inspect(verify, canonical.stage, Kind::Driver, kCdcCanonicalId, candidate) ||
      !OrdinaryTransactionDetail::inspect(verify, alias.target, Kind::Driver, kCdcAliasId, retired))
    return false;
  CdcMigrationIntent intent{};
  std::strcpy(intent.candidateVersion, candidate.version);
  std::strcpy(intent.aliasVersion, retired.version);
  if (!CdcMigrationDetail::valid(intent) || !ops.manifestDigest(canonical.stage, intent.manifestDigest) ||
      !ops.manifestDigest(alias.target, intent.aliasManifestDigest))
    return false;
  return ops.removeMatchingPart(intent);
}

}  // namespace
bool cdcMigrationPendingOnSd() {
  Ops ops;
  return Storage.ready() && cdcMigrationPending(ops);
}
OrdinaryTransactionResult reconcileCdcMigrationFromSd(const PackageRuntimePolicy& policy,
                                                      uint32_t (*resolver)(const char*), Identity& observed) {
  Ops ops;
  if (!Storage.ready() || !resolver || ops.legacyPending()) return OrdinaryTransactionResult::AmbiguousState;
  if (!cdcMigrationPending(ops)) return OrdinaryTransactionResult::NoInstalledPackage;
  PackageReplacementLease canonical(kCdcCanonicalRoot), alias(kCdcAliasRoot);
  if (!canonical || !alias) return OrdinaryTransactionResult::InUse;
  if (cleanPrecommitPart(ops, Verify{policy, resolver})) return OrdinaryTransactionResult::NoInstalledPackage;
  return CdcMigrationDetail::recoverLocked(ops, Verify{policy, resolver}, Purge{&ops}, observed);
}
OrdinaryTransactionResult recoverCanonicalCdcFromSd(const PackageRuntimePolicy& policy,
                                                    uint32_t (*resolver)(const char*), Identity& observed) {
  Ops ops;
  if (!Storage.ready() || !resolver || ops.legacyPending()) return OrdinaryTransactionResult::AmbiguousState;
  if (cdcMigrationPending(ops)) {
    const auto result = reconcileCdcMigrationFromSd(policy, resolver, observed);
    ops.refresh();
    if (cdcMigrationPending(ops)) return result;
  }
  return recoverOrdinaryPackage(ops, Kind::Driver, kCdcCanonicalId, Verify{policy, resolver}, Purge{&ops}, observed);
}
OrdinaryTransactionResult publishCanonicalCdcFromSd(const Identity& candidate, const PackageRuntimePolicy& policy,
                                                    uint32_t (*resolver)(const char*), Identity& observed) {
  Ops ops;
  return publishCdcPackage(ops, candidate, Verify{policy, resolver}, Purge{&ops}, observed);
}
bool previewCanonicalCdcFromSd(const Identity& candidate, const PackageRuntimePolicy& policy,
                               uint32_t (*resolver)(const char*), Identity& installed, bool& allowed) {
  installed = {};
  allowed = false;
  Ops ops;
  if (!Storage.ready() || !resolver || candidate.kind != Kind::Driver || std::strcmp(candidate.id, kCdcCanonicalId) ||
      !safeVersion(candidate.version) || cdcMigrationPending(ops) || ops.legacyPending())
    return false;
  bool blocked = false;
  for (const char* id : {kCdcCanonicalId, kCdcAliasId}) {
    OrdinaryTransactionPaths paths{};
    ordinaryTransactionPaths(Kind::Driver, id, paths);
    if (ops.exists(paths.backup) || ops.exists(paths.removing)) return false;
    blocked = blocked || ops.exists(paths.stage) || systemPackageUseGate().pinned(paths.target);
    if (!ops.exists(paths.target)) continue;
    Identity actual{};
    if (!verifyManagedOrdinarySdDirectory(paths.target, Kind::Driver, id, policy, resolver, actual, false))
      return false;
    if (!installed.id[0] || comparePackageVersions(actual.version, installed.version) == VersionOrder::Newer)
      installed = actual;
  }
  allowed = !blocked &&
            (!installed.id[0] || comparePackageVersions(candidate.version, installed.version) == VersionOrder::Newer);
  return true;
}
}  // namespace RuntimePackages
