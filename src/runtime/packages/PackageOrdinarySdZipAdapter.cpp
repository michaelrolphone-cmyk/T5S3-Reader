#include "PackageOrdinarySdZipAdapter.h"
#include "PackageOrdinarySdAdapter.h"
#include "PackageRteZipInstall.h"

#include <HalStorage.h>
#include <mbedtls/sha256.h>

#include <cctype>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>

namespace RuntimePackages {
namespace {
constexpr size_t kManifestCapacity = 4096;

bool safeArchivePath(const char* path) {
  if (!path || path[0] != '/' || !path[1] ||
      std::strncmp(path, "/sd/", 4) == 0) return false;
  const size_t length = std::strlen(path);
  if (length >= 120 || length < 9 ||
      std::strcmp(path + length - 8, ".rte.zip")) return false;
  size_t segment = 0;
  for (size_t i = 1; i < length; ++i) {
    const unsigned char c = static_cast<unsigned char>(path[i]);
    if (c == '/') {
      if (!segment || (segment == 1 && path[i - 1] == '.') ||
          (segment == 2 && path[i - 2] == '.' && path[i - 1] == '.')) return false;
      segment = 0;
    } else {
      if (c >= 128 || (!std::isalnum(c) && c != '_' && c != '-' && c != '.'))
        return false;
      ++segment;
    }
  }
  return segment > 0;
}

bool readFile(const std::string& path, uint64_t offset, uint8_t* out, size_t count) {
  if (!out || !count) return false;
  HalFile file = Storage.open(path.c_str(), O_RDONLY);
  if (!file.isOpen() || file.isDirectory()) {
    if (file.isOpen()) (void)file.close();
    return false;
  }
  const uint64_t length = file.fileSize64();
  const bool ok = offset <= length && count <= length - offset &&
      file.seek64(offset) && file.read(out, count) == static_cast<int>(count);
  return file.close() && ok;
}

class Archive {
 public:
  explicit Archive(const char* path) : file_(Storage.open(path, O_RDONLY)) {
    if (file_.isOpen() && !file_.isDirectory()) length_ = file_.fileSize64();
  }
  ~Archive() { if (file_.isOpen()) (void)file_.close(); }
  bool valid() const {
    // At most 4 MiB of stored entries plus bounded ZIP headers/directories.
    return file_.isOpen() && !file_.isDirectory() && length_ >= kRteZipEocdBytes &&
           length_ <= kRteZipMaxTotalBytes + 8192u;
  }
  uint64_t size() const { return length_; }
  bool readAt(uint64_t offset, uint8_t* out, size_t count) {
    if (offset > length_ || count > length_ - offset || (count && !out)) return false;
    return !count || (file_.seek64(offset) &&
                      file_.read(out, count) == static_cast<int>(count));
  }
 private:
  HalFile file_;
  uint64_t length_ = 0;
};

class Hash {
 public:
  Hash() { mbedtls_sha256_init(&ctx_); }
  ~Hash() { mbedtls_sha256_free(&ctx_); }
  bool start() { return mbedtls_sha256_starts_ret(&ctx_, 0) == 0; }
  bool update(const uint8_t* bytes, size_t count) {
    return mbedtls_sha256_update_ret(&ctx_, bytes, count) == 0;
  }
  bool finish(uint8_t out[32]) { return mbedtls_sha256_finish_ret(&ctx_, out) == 0; }
 private:
  mbedtls_sha256_context ctx_{};
};

// Reject unexpected files before deleting anything, including from an
// interrupted cleanup. A missing manifest is tolerated only for an OWNED
// partial stage or a directory from which all declared entries are gone.
bool inventory(const char* root, const OrdinaryPackagePlan& plan, bool complete) {
  if (!root || plan.entryCount > kMaxPackageEntries) return false;
  HalFile dir = Storage.open(root, O_RDONLY);
  if (!dir.isOpen() || !dir.isDirectory()) {
    if (dir.isOpen()) (void)dir.close();
    return false;
  }
  bool seen[kMaxPackageEntries]{}, manifest = false, valid = true;
  size_t count = 0;
  while (valid) {
    HalFile entry = dir.openNextFile();
    if (!entry.isOpen()) break;
    char name[128]{};
    const size_t length = entry.getName(name, sizeof(name));
    if (!length || length >= sizeof(name) || entry.isDirectory()) valid = false;
    else if (!std::strcmp(name, kOrdinaryManifestName)) {
      if (manifest) valid = false;
      manifest = true;
    } else {
      bool found = false;
      for (size_t i = 0; i < plan.entryCount; ++i) {
        if (std::strcmp(name, plan.entries[i].name)) continue;
        if (seen[i]) valid = false;
        seen[i] = found = true;
        break;
      }
      if (!found) valid = false;
    }
    (void)entry.close();
    if (++count > plan.entryCount + 1) valid = false;
  }
  const bool closed = dir.close();
  if (!closed || !valid) return false;
  if (!complete) return true;
  if (!manifest || count != plan.entryCount + 1) return false;
  for (size_t i = 0; i < plan.entryCount; ++i) if (!seen[i]) return false;
  return true;
}

bool removeKnown(const char* root, const OrdinaryPackagePlan& plan) {
  if (!inventory(root, plan, false)) return false;
  for (size_t i = 0; i < plan.entryCount; ++i) {
    const std::string filename = std::string(root) + "/" + plan.entries[i].name;
    if (Storage.exists(filename.c_str()) && !Storage.remove(filename.c_str()))
      return false;
  }
  const std::string manifest = std::string(root) + "/" + kOrdinaryManifestName;
  if (Storage.exists(manifest.c_str()) && !Storage.remove(manifest.c_str()))
    return false;
  return Storage.rmdir(root);
}

class Stage {
 public:
  ~Stage() { if (writer_.isOpen()) (void)writer_.close(); }
  bool begin(const OrdinaryPackagePlan& plan) {
    OrdinaryTransactionPaths paths{};
    if (owned_ || !ordinaryTransactionPaths(plan.identity.kind, plan.identity.id, paths))
      return false;
    const std::string target(paths.target);
    const std::string root = target.substr(0, target.find_last_of('/'));
    if ((!Storage.exists(root.c_str()) && !Storage.mkdir(root.c_str(), false)) ||
        Storage.exists(paths.stage) || !Storage.mkdir(paths.stage, false)) return false;
    root_ = paths.stage;
    plan_ = plan;
    owned_ = true;
    return true;
  }
  bool beginEntry(const char* name, uint64_t) {
    if (!owned_ || writer_.isOpen() || !name) return false;
    writer_ = Storage.open((root_ + "/" + name).c_str(), O_WRONLY | O_CREAT | O_EXCL);
    return writer_.isOpen() && !writer_.isDirectory();
  }
  bool append(const uint8_t* bytes, size_t count) {
    return writer_.isOpen() && writer_.write(bytes, count) == count;
  }
  bool endEntry() { return writer_.isOpen() && writer_.close(); }
  bool readEntry(const char* name, uint64_t at, uint8_t* out, size_t count) {
    return owned_ && readFile(root_ + "/" + name, at, out, count);
  }
  bool writeManifest(const uint8_t* data, size_t count) {
    if (!owned_ || writer_.isOpen() || !data || !count || count > kManifestCapacity)
      return false;
    HalFile file = Storage.open((root_ + "/" + kOrdinaryManifestName).c_str(),
                                O_WRONLY | O_CREAT | O_EXCL);
    if (!file.isOpen() || file.isDirectory()) {
      if (file.isOpen()) (void)file.close();
      return false;
    }
    const bool written = file.write(data, count) == count;
    return file.close() && written;
  }
  bool seal() { return owned_ && !writer_.isOpen() &&
                       inventory(root_.c_str(), plan_, true); }
  bool discard() {
    if (!owned_ || (writer_.isOpen() && !writer_.close()) ||
        !removeKnown(root_.c_str(), plan_)) return false;
    owned_ = false;
    return true;
  }
 private:
  std::string root_;
  OrdinaryPackagePlan plan_{};
  HalFile writer_;
  bool owned_ = false;
};

struct Ops {
  bool exists(const char* path) const { return Storage.exists(path); }
  bool rename(const char* src, const char* dst) const { return Storage.rename(src, dst); }
};

bool purgeManaged(const char* path, Kind kind, const char* expectedId) {
  if (!path || !safeId(expectedId)) return false;
  const std::string filename = std::string(path) + "/" + kOrdinaryManifestName;
  if (!Storage.exists(filename.c_str())) {
    OrdinaryPackagePlan empty{};
    return removeKnown(path, empty); // Never delete a directory containing unknown data.
  }
  HalFile file = Storage.open(filename.c_str(), O_RDONLY);
  if (!file.isOpen() || file.isDirectory()) {
    if (file.isOpen()) (void)file.close();
    return false;
  }
  const uint64_t bytes = file.fileSize64();
  if (!bytes || bytes > kManifestCapacity) { (void)file.close(); return false; }
  std::unique_ptr<char[]> text(new (std::nothrow) char[kManifestCapacity]{});
  std::unique_ptr<OrdinaryPackagePlan> plan(new (std::nothrow) OrdinaryPackagePlan{});
  if (!text || !plan) { (void)file.close(); return false; }
  const bool read = file.read(reinterpret_cast<uint8_t*>(text.get()),
                               static_cast<size_t>(bytes)) == static_cast<int>(bytes);
  if (!file.close() || !read ||
      !parseOrdinaryManifest(text.get(), static_cast<size_t>(bytes), *plan) ||
      plan->identity.kind != kind || std::strcmp(plan->identity.id, expectedId))
    return false;
  return removeKnown(path, *plan);
}
} // namespace

OrdinaryInstallOutcome installOrdinaryFromSdZip(
    const char* archivePath, const PackageRuntimePolicy& policy,
    uint32_t (*resolveCapability)(const char*), const Identity* expected) {
  OrdinaryInstallOutcome invalid{};
  if (!Storage.ready() || !safeArchivePath(archivePath) || !resolveCapability)
    return invalid;
  Archive archive(archivePath);
  if (!archive.valid()) return invalid;
  auto readAt = [&archive](uint64_t at, uint8_t* out, size_t count) {
    return archive.readAt(at, out, count);
  };
  Kind kind{};
  std::string id;
  {
    // Determine manager-derived paths without trusting an archive filename or
    // catalog entry; the same ZIP parser will independently reparse below.
    std::unique_ptr<RteZipView> zip(new (std::nothrow) RteZipView{});
    std::unique_ptr<OrdinaryPackagePlan> plan(new (std::nothrow) OrdinaryPackagePlan{});
    std::unique_ptr<uint8_t[]> manifest(new (std::nothrow) uint8_t[kManifestCapacity]{});
    if (!zip || !plan || !manifest ||
        inspectRteZip(readAt, archive.size(), *zip) != RteZipResult::Ready ||
        planRteZip(readAt, *zip, manifest.get(), kManifestCapacity, *plan) !=
            RteZipResult::Ready) return invalid;
    const Identity& candidate = plan->identity;
    if (expected && (candidate.kind != expected->kind ||
                     std::strcmp(candidate.id, expected->id) ||
                     std::strcmp(candidate.version, expected->version) ||
                     std::strcmp(candidate.artifact, expected->artifact))) return invalid;
    kind = candidate.kind;
    id = candidate.id;
  }
  OrdinaryTransactionPaths paths{};
  if (!ordinaryTransactionPaths(kind, id.c_str(), paths)) return invalid;
  if (kind == Kind::Driver) {
    const std::string legacy = std::string("/Drivers/.") + id;
    if (Storage.exists((legacy + ".install").c_str()) ||
        Storage.exists((legacy + ".previous").c_str())) return invalid;
  }
  std::unique_ptr<Stage> destination(new (std::nothrow) Stage());
  std::unique_ptr<uint8_t[]> manifest(new (std::nothrow) uint8_t[kManifestCapacity]{});
  if (!destination || !manifest) return invalid;
  Hash hash;
  Ops ops;
  uint8_t io[kOrdinaryIoBytes]{};
  const auto verify = [&policy, resolveCapability](const char* path, Identity& observed) {
    return verifyOrdinarySdDirectory(path, policy, resolveCapability, observed);
  };
  const auto purge = [kind, &id](const char* path) {
    return purgeManaged(path, kind, id.c_str());
  };
  // Archive CRC/topology, SHA-256, ABI/import, stage verification and
  // generation-preserving publication are all checked inside this call.
  return installOrdinaryFromRteZip(readAt, archive.size(), manifest.get(),
      kManifestCapacity, *destination, hash, resolveCapability, policy,
      io, ops, verify, purge, true);
}

} // namespace RuntimePackages
