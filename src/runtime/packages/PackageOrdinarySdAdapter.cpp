#include "PackageOrdinarySdAdapter.h"
#include "PackageOrdinaryManagedInstall.h"
#include "runtime/drivers/DriverPackage.h"

#include <HalStorage.h>
#include <NativeAppLauncher.h>
#include <mbedtls/sha256.h>

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace RuntimePackages {
namespace {
constexpr size_t kManifestBytes = 4096;

// Source roots are Storage paths, never /sd VFS paths. Entry basenames are
// independently checked by manifest preflight. No traversal or hidden aliases.
bool safeSourcePath(const char* path) {
  if (!path || path[0] != '/' || !path[1] ||
      std::strncmp(path, "/sd/", 4) == 0) return false;
  size_t length = 0, segment = 0;
  while (path[length]) {
    const unsigned char c = static_cast<unsigned char>(path[length]);
    if (++length >= 120) return false;
    if (c == '/') {
      if (length != 1 && (!segment ||
          (segment == 1 && path[length - 2] == '.') ||
          (segment == 2 && path[length - 3] == '.' && path[length - 2] == '.')))
        return false;
      segment = 0;
    } else {
      if (c >= 128 || (!std::isalnum(c) && c != '_' && c != '-' && c != '.')) return false;
      ++segment;
    }
  }
  return segment && !(segment == 1 && path[length - 1] == '.') &&
         !(segment == 2 && path[length - 2] == '.' && path[length - 1] == '.');
}
bool directoryExists(const char* path) {
  HalFile directory = Storage.open(path, O_RDONLY);
  const bool okay = directory.isOpen() && directory.isDirectory();
  if (directory.isOpen()) (void)directory.close();
  return okay;
}
bool sizeOf(const std::string& path, uint64_t& size) {
  size = 0;
  HalFile file = Storage.open(path.c_str(), O_RDONLY);
  if (!file.isOpen() || file.isDirectory()) {
    if (file.isOpen()) (void)file.close();
    return false;
  }
  size = file.fileSize64();
  return file.close();
}
bool readAt(const std::string& path, uint64_t offset,
            uint8_t* data, size_t length) {
  if (!data || !length) return false;
  HalFile file = Storage.open(path.c_str(), O_RDONLY);
  if (!file.isOpen() || file.isDirectory()) {
    if (file.isOpen()) (void)file.close();
    return false;
  }
  const uint64_t bytes = file.fileSize64();
  const bool okay = offset <= bytes && length <= bytes - offset &&
      file.seek64(offset) && file.read(data, length) == static_cast<int>(length);
  return file.close() && okay;
}
bool readManifest(const char* directory, const char* name,
                  char (&buffer)[kManifestBytes], size_t& length) {
  length = 0;
  if (!directory) return false;
  const std::string filename = std::string(directory) + "/" + name;
  uint64_t bytes = 0;
  if (!sizeOf(filename, bytes) || !bytes || bytes > sizeof(buffer)) return false;
  length = static_cast<size_t>(bytes);
  if (readAt(filename, 0, reinterpret_cast<uint8_t*>(buffer), length)) return true;
  length = 0;
  return false;
}

// A partial OWNED stage may lack some declared files. For all other paths a
// missing manifest plus nonempty directory is ambiguous and never purged.
bool inventory(const char* root, const OrdinaryPackagePlan& plan, bool full) {
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
    const size_t n = entry.getName(name, sizeof(name));
    if (!n || n >= sizeof(name) || entry.isDirectory()) valid = false;
    else if (std::strcmp(name, kOrdinaryManifestName) == 0) {
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
  if (!full) return true;
  if (!manifest || count != plan.entryCount + 1) return false;
  for (size_t i = 0; i < plan.entryCount; ++i) if (!seen[i]) return false;
  return true;
}
bool purgeKnown(const char* root, const OrdinaryPackagePlan& plan,
                bool ownedPartial) {
  if (!inventory(root, plan, false)) return false;
  const std::string manifest = std::string(root) + "/" + kOrdinaryManifestName;
  const bool exists = Storage.exists(manifest.c_str());
  if (!exists && !ownedPartial && plan.entryCount) return false;
  // Deleting only explicitly declared files allows cleanup of a partially
  // written, exclusively owned stage without ever traversing user entries.
  for (size_t i = 0; i < plan.entryCount; ++i) {
    const std::string filename = std::string(root) + "/" + plan.entries[i].name;
    if (Storage.exists(filename.c_str()) && !Storage.remove(filename.c_str())) return false;
  }
  if (exists && !Storage.remove(manifest.c_str())) return false;
  return Storage.rmdir(root);
}

// Legacy first-party driver generations are compatible with canonical
// upgrades. Only an independently verified id/version/ELF+digest is accepted;
// the old .install/.previous layout must first be recovered by its owner.
bool legacyInventory(const char* path, bool full) {
  HalFile dir = Storage.open(path, O_RDONLY);
  if (!dir.isOpen() || !dir.isDirectory()) {
    if (dir.isOpen()) (void)dir.close();
    return false;
  }
  bool elf = false, manifest = false, good = true;
  size_t count = 0;
  while (good) {
    HalFile file = dir.openNextFile();
    if (!file.isOpen()) break;
    char name[128]{};
    const size_t length = file.getName(name, sizeof(name));
    if (!length || length >= sizeof(name) || file.isDirectory()) good = false;
    else if (!std::strcmp(name, "driver.elf") && !elf) elf = true;
    else if (!std::strcmp(name, "manifest.json") && !manifest) manifest = true;
    else good = false;
    (void)file.close();
    if (++count > 2) good = false;
  }
  const bool closed = dir.close();
  return good && closed && (!full || (count == 2 && elf && manifest));
}
bool legacyIdentity(const char* path, const char* id, Identity& observed) {
  observed = {};
  if (!id || !legacyInventory(path, true) ||
      native_app_register_sd_vfs() != ESP_OK) return false;
  char buffer[kManifestBytes]{};
  size_t length = 0;
  if (!readManifest(path, "manifest.json", buffer, length)) return false;
  const std::string json(buffer, length);
  DriverPackageInfo info{};
  const std::string vfs = std::string("/sd") + path + "/driver.elf";
  if (!validateDriverPayload(json, vfs.c_str(), &info) ||
      std::strcmp(info.id, id)) return false;
  return makeIdentity(Kind::Driver, info.id, info.version,
                      "driver.elf", false, &observed);
}
bool purgeLegacy(const char* path, const char* id) {
  if (!legacyInventory(path, false)) return false;
  const std::string manifest = std::string(path) + "/manifest.json";
  if (!Storage.exists(manifest.c_str())) {
    // The legacy manifest is also removed last; no manifest means empty only.
    return Storage.rmdir(path);
  }
  char buffer[kManifestBytes]{};
  size_t length = 0;
  DriverPackageInfo info{};
  if (!readManifest(path, "manifest.json", buffer, length) ||
      !parseDriverPackageManifest(std::string(buffer, length), info) ||
      std::strcmp(info.id, id)) return false;
  const std::string elf = std::string(path) + "/driver.elf";
  if (Storage.exists(elf.c_str()) && !Storage.remove(elf.c_str())) return false;
  return Storage.remove(manifest.c_str()) && Storage.rmdir(path);
}

class SdSource {
 public:
  explicit SdSource(const char* path) : root_(path) {}
  bool entrySize(const char* name, uint64_t& size) {
    return sizeOf(root_ + "/" + name, size);
  }
  bool readAt(const char* name, uint64_t offset, uint8_t* data, size_t length) {
    return RuntimePackages::readAt(root_ + "/" + name, offset, data, length);
  }
 private:
  std::string root_;
};
class SdHash {
 public:
  SdHash() { mbedtls_sha256_init(&ctx_); }
  ~SdHash() { mbedtls_sha256_free(&ctx_); }
  bool start() { return mbedtls_sha256_starts_ret(&ctx_, 0) == 0; }
  bool update(const uint8_t* data, size_t length) {
    return mbedtls_sha256_update_ret(&ctx_, data, length) == 0;
  }
  bool finish(uint8_t output[32]) {
    return mbedtls_sha256_finish_ret(&ctx_, output) == 0;
  }
 private:
  mbedtls_sha256_context ctx_{};
};
class SdDirectory {
 public:
  explicit SdDirectory(const char* path) : root_(path) {}
  bool readManifest(char* output, size_t capacity, size_t& length) {
    if (!output || capacity < kManifestBytes) return false;
    char metadata[kManifestBytes]{};
    if (!::RuntimePackages::readManifest(root_.c_str(), kOrdinaryManifestName,
                                         metadata, length)) return false;
    std::memcpy(output, metadata, length);
    return true;
  }
  bool exactEntries(const OrdinaryPackagePlan& plan) {
    return inventory(root_.c_str(), plan, true);
  }
  bool entrySize(const char* name, uint64_t& size) {
    return sizeOf(root_ + "/" + name, size);
  }
  bool readAt(const char* name, uint64_t offset, uint8_t* data, size_t length) {
    return ::RuntimePackages::readAt(root_ + "/" + name, offset, data, length);
  }
 private:
  std::string root_;
};
class SdStage {
 public:
  ~SdStage() {
    if (writer_.isOpen()) (void)writer_.close();
    // A failed cleanup leaves its stage inspectable after restart.
  }
  bool begin(const OrdinaryPackagePlan& plan) {
    OrdinaryTransactionPaths paths{};
    if (owns_ || !ordinaryTransactionPaths(plan.identity.kind,
                                            plan.identity.id, paths)) return false;
    const std::string target(paths.target);
    const std::string root = target.substr(0, target.find_last_of('/'));
    if ((!Storage.exists(root.c_str()) && !Storage.mkdir(root.c_str(), false)) ||
        !directoryExists(root.c_str()) || Storage.exists(paths.stage) ||
        !Storage.mkdir(paths.stage, false)) return false;
    root_ = paths.stage;
    plan_ = plan;
    owns_ = true;
    return true;
  }
  bool beginEntry(const char* name, uint64_t) {
    if (!owns_ || writer_.isOpen()) return false;
    const std::string path = root_ + "/" + name;
    writer_ = Storage.open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL);
    return writer_.isOpen() && !writer_.isDirectory();
  }
  bool append(const uint8_t* data, size_t length) {
    return writer_.isOpen() && writer_.write(data, length) == length;
  }
  bool endEntry() { return writer_.isOpen() && writer_.close(); }
  bool readEntry(const char* name, uint64_t offset, uint8_t* data, size_t length) {
    return owns_ && ::RuntimePackages::readAt(root_ + "/" + name,
                                               offset, data, length);
  }
  bool writeManifest(const uint8_t* metadata, size_t length) {
    if (!owns_ || !metadata || !length || length > kManifestBytes ||
        writer_.isOpen()) return false;
    const std::string path = root_ + "/" + kOrdinaryManifestName;
    HalFile file = Storage.open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL);
    if (!file.isOpen() || file.isDirectory()) {
      if (file.isOpen()) (void)file.close();
      return false;
    }
    const bool written = file.write(metadata, length) == length;
    return file.close() && written;
  }
  bool seal() {
    if (!owns_ || writer_.isOpen()) return false;
    SdDirectory directory(root_.c_str());
    char metadata[kManifestBytes]{};
    size_t length = 0;
    return directory.readManifest(metadata, sizeof(metadata), length) &&
           inventory(root_.c_str(), plan_, true);
  }
  bool discard() {
    if (!owns_ || (writer_.isOpen() && !writer_.close()) ||
        !purgeKnown(root_.c_str(), plan_, true)) return false;
    owns_ = false;
    return true;
  }
 private:
  std::string root_;
  OrdinaryPackagePlan plan_{};
  HalFile writer_;
  bool owns_ = false;
};
struct Ops {
  bool exists(const char* path) const { return Storage.exists(path); }
  bool rename(const char* from, const char* to) const {
    return Storage.rename(from, to);
  }
};
bool verifyCanonical(const char* path, const PackageRuntimePolicy& policy,
                     uint32_t (*resolver)(const char*), Identity& observed) {
  if (!path || !resolver || !directoryExists(path)) return false;
  SdDirectory directory(path);
  SdHash hash;
  uint8_t io[kOrdinaryIoBytes]{};
  return verifyCanonicalOrdinaryDirectory(directory, hash, resolver, policy,
                                          io, observed);
}
bool purgeManaged(const char* path, Kind kind, const char* expectedId) {
  if (!path || !safeId(expectedId) || !directoryExists(path)) return false;
  const std::string canonical = std::string(path) + "/" + kOrdinaryManifestName;
  if (Storage.exists(canonical.c_str())) {
    char metadata[kManifestBytes]{};
    size_t length = 0;
    OrdinaryPackagePlan plan{};
    if (!readManifest(path, kOrdinaryManifestName, metadata, length) ||
        !parseOrdinaryManifest(metadata, length, plan) ||
        plan.identity.kind != kind || std::strcmp(plan.identity.id, expectedId))
      return false;
    return purgeKnown(path, plan, false);
  }
  const std::string legacy = std::string(path) + "/manifest.json";
  if (kind == Kind::Driver && Storage.exists(legacy.c_str()))
    return purgeLegacy(path, expectedId);
  // A missing manifest is only safe after previous manifest-last cleanup has
  // removed every file. Never erase unrecognized data or partial ELF bytes.
  OrdinaryPackagePlan empty{};
  return purgeKnown(path, empty, false);
}
} // namespace

bool verifyOrdinarySdDirectory(const char* managedDirectory,
    const PackageRuntimePolicy& policy,
    uint32_t (*resolveCapability)(const char*), Identity& observed) {
  observed = {};
  return Storage.ready() && safeSourcePath(managedDirectory) &&
      verifyCanonical(managedDirectory, policy, resolveCapability, observed);
}
OrdinaryInstallOutcome installOrdinaryFromSd(
    const char* sourceDirectory, const PackageRuntimePolicy& policy,
    uint32_t (*resolveCapability)(const char*)) {
  OrdinaryInstallOutcome invalid{};
  if (!Storage.ready() || !safeSourcePath(sourceDirectory) ||
      !resolveCapability || !directoryExists(sourceDirectory)) return invalid;
  char metadata[kManifestBytes]{};
  size_t length = 0;
  OrdinaryPackagePlan plan{};
  if (!readManifest(sourceDirectory, kOrdinaryManifestName, metadata, length) ||
      !parseOrdinaryManifest(metadata, length, plan) ||
      !inventory(sourceDirectory, plan, true)) return invalid;
  const Kind kind = plan.identity.kind;
  const std::string id(plan.identity.id);
  OrdinaryTransactionPaths paths{};
  if (!ordinaryTransactionPaths(kind, id.c_str(), paths)) return invalid;
  if (kind == Kind::Driver) {
    // Existing Driver Manager owns migration of .install/.previous. Never
    // silently treat an unresolved old generation as a fresh install.
    const std::string legacy = std::string("/Drivers/.") + id;
    if (Storage.exists((legacy + ".install").c_str()) ||
        Storage.exists((legacy + ".previous").c_str())) return invalid;
  }
  SdSource source(sourceDirectory);
  SdStage destination;
  SdHash hash;
  Ops ops;
  uint8_t io[kOrdinaryIoBytes]{};
  const auto verify = [&policy, resolveCapability, kind, &id, &paths](
      const char* path, Identity& observed) {
    if (verifyCanonical(path, policy, resolveCapability, observed)) return true;
    if (kind != Kind::Driver ||
        (std::strcmp(path, paths.target) && std::strcmp(path, paths.backup)))
      return false;
    return legacyIdentity(path, id.c_str(), observed);
  };
  const auto purge = [kind, &id](const char* path) {
    return purgeManaged(path, kind, id.c_str());
  };
  // The caller must own the per-identity manager mutation lock; publication
  // itself takes an exclusive mapping lease and independently re-verifies.
  return installCanonicalOrdinaryPackage(metadata, length, source, destination,
      hash, resolveCapability, policy, io, ops, verify, purge, true);
}
} // namespace RuntimePackages
