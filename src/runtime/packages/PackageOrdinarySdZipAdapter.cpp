#include "PackageCdcSdMigration.h"
#include "PackageOrdinarySdZipAdapter.h"
#include "PackageOrdinarySdAdapter.h"
#include "PackageOrdinarySdTree.h"
#include "PackageVerificationReceiptSd.h"
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
  if (length >= 192 || length < 9 ||
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
           length_ <= kRteZipMaxTotalBytes + 65536u;
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

// Reuse the same declared-tree inventory and manifest-last cleanup as ordinary
// directory intake. ZIP transport must not silently restore flat-only staging.
bool inventory(const char* root, const OrdinaryPackagePlan& plan, bool complete) {
  OrdinarySdTreeOps ops(root);
  return ordinaryTreeInventory(plan, ops, complete, true);
}
bool removeKnown(const char* root, const OrdinaryPackagePlan& plan) {
  OrdinarySdTreeOps ops(root);
  return purgeOrdinaryTree(plan, ops, true, true);
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
    OrdinarySdTreeOps directories(root_.c_str());
    if (!directories.createParents(name)) return false;
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
    if (!file.close() || !written) return false;
    receipt_ = writeVerifiedStageReceipt(root_.c_str(), plan_, data, count);
    return receipt_ == ReceiptWriteResult::Complete;
  }
  bool seal() { return owned_ && !writer_.isOpen() && receipt_ == ReceiptWriteResult::Complete &&
                       inventory(root_.c_str(), plan_, true); }
  bool discard() {
    if (!owned_ || receipt_ == ReceiptWriteResult::CloseUncertain || (writer_.isOpen() && !writer_.close()) ||
        !removeKnown(root_.c_str(), plan_)) return false;
    owned_ = false;
    return true;
  }
 private:
  std::string root_;
  OrdinaryPackagePlan plan_{};
  HalFile writer_;
  bool owned_ = false;
  ReceiptWriteResult receipt_ = ReceiptWriteResult::Failed;
};

struct Ops {
  bool exists(const char* path) const { return Storage.exists(path); }
  bool rename(const char* src, const char* dst) const { return Storage.rename(src, dst); }
};

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
                     std::strcmp(candidate.artifact, expected->artifact) ||
                     candidate.payload != expected->payload)) return invalid;
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
  const auto verify = [&policy, resolveCapability, kind, &id](const char* path, Identity& observed) {
    return verifyManagedOrdinarySdDirectory(path, kind, id.c_str(), policy, resolveCapability, observed);
  };
  const auto purge = [kind, &id](const char* path) {
    return purgeManagedOrdinarySdDirectory(path, kind, id.c_str());
  };
  // Archive CRC/topology, SHA-256, ABI/import, stage verification and
  // generation-preserving publication are all checked inside this call.
  return installOrdinaryFromRteZip(readAt, archive.size(), manifest.get(),
      kManifestCapacity, *destination, hash, resolveCapability, policy,
      io, ops, verify, purge, true, OrdinarySdLineageTransaction{policy, resolveCapability});
}

} // namespace RuntimePackages
