#include "PackageOrdinarySdAdapter.h"
#include "PackageOrdinaryManagedInstall.h"

#include <HalStorage.h>
#include <mbedtls/sha256.h>

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace RuntimePackages {
namespace {
constexpr size_t kManifestBytes = 4096;

// The source is a directory selected by the manager, never an untrusted ELF
// pathname. No relative path, traversal, /sd VFS prefix or ambiguous FAT
// separator is accepted. Entry names are separately validated by preflight.
bool safeSourcePath(const char* path) {
  if (!path || path[0] != '/' || !path[1] ||
      std::strncmp(path, "/sd/", 4) == 0) return false;
  size_t length = 0;
  size_t segment = 0;
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
      if (!std::isalnum(c) && c != '_' && c != '-' && c != '.') return false;
      ++segment;
    }
  }
  return segment != 0 && !(segment == 1 && path[length - 1] == '.') &&
         !(segment == 2 && path[length - 2] == '.' && path[length - 1] == '.');
}

bool storageDirectory(const char* path) {
  HalFile dir = Storage.open(path, O_RDONLY);
  const bool okay = dir.isOpen() && dir.isDirectory();
  if (dir.isOpen()) (void)dir.close();
  return okay;
}
bool readExact(const std::string& path, uint64_t offset,
               uint8_t* output, size_t count) {
  if (!output || !count) return false;
  HalFile file = Storage.open(path.c_str(), O_RDONLY);
  if (!file.isOpen() || file.isDirectory()) {
    if (file.isOpen()) (void)file.close();
    return false;
  }
  const uint64_t size = file.fileSize64();
  const bool valid = offset <= size && count <= size - offset &&
      file.seek64(offset) && file.read(output, count) == static_cast<int>(count);
  const bool closed = file.close();
  return valid && closed;
}
bool fileSize(const std::string& path, uint64_t& size) {
  size = 0;
  HalFile file = Storage.open(path.c_str(), O_RDONLY);
  if (!file.isOpen() || file.isDirectory()) {
    if (file.isOpen()) (void)file.close();
    return false;
  }
  size = file.fileSize64();
  return file.close();
}
bool manifestBytes(const char* root, char (&buffer)[kManifestBytes],
                   size_t& length) {
  length = 0;
  if (!root) return false;
  const std::string path = std::string(root) + "/" + kOrdinaryManifestName;
  uint64_t size = 0;
  if (!fileSize(path, size) || !size || size > sizeof(buffer)) return false;
  length = static_cast<size_t>(size);
  if (!readExact(path, 0, reinterpret_cast<uint8_t*>(buffer), length)) {
    length = 0;
    return false;
  }
  return true;
}

// Directory inventory is checked both before staging and after every rename.
// A second scan after hashing catches unexpected added files and directories.
// This is FAT-safe: duplicate names and nested directories are forbidden.
bool inventory(const char* root, const OrdinaryPackagePlan& plan,
               bool requireComplete) {
  if (!root || plan.entryCount > kMaxPackageEntries) return false;
  HalFile dir = Storage.open(root, O_RDONLY);
  if (!dir.isOpen() || !dir.isDirectory()) {
    if (dir.isOpen()) (void)dir.close();
    return false;
  }
  bool seen[kMaxPackageEntries]{};
  bool manifest = false, valid = true;
  size_t count = 0;
  while (valid) {
    HalFile item = dir.openNextFile();
    if (!item.isOpen()) break;
    char name[128]{};
    const size_t length = item.getName(name, sizeof(name));
    if (!length || length >= sizeof(name) || item.isDirectory()) {
      valid = false;
    } else if (std::strcmp(name, kOrdinaryManifestName) == 0) {
      if (manifest) valid = false;
      manifest = true;
    } else {
      bool found = false;
      for (size_t i = 0; i < plan.entryCount; ++i) {
        if (std::strcmp(name, plan.entries[i].name) != 0) continue;
        if (seen[i]) valid = false;
        seen[i] = true;
        found = true;
        break;
      }
      if (!found) valid = false;
    }
    (void)item.close();
    ++count;
    if (count > plan.entryCount + 1) valid = false;
  }
  const bool closed = dir.close();
  if (!closed || !valid) return false;
  if (!requireComplete) return true;
  if (!manifest || count != plan.entryCount + 1) return false;
  for (size_t i = 0; i < plan.entryCount; ++i) if (!seen[i]) return false;
  return true;
}

// Only manager-owned entries are deleted. Manifest is removed LAST, allowing
// reboot recovery to identify partially purged backups/tombstones. If it is
// already absent, only an empty directory may be removed. No recursive rm.
bool purgeKnown(const char* root, const OrdinaryPackagePlan& plan,
                bool allowMissingManifest) {
  if (!inventory(root, plan, false)) return false;
  const std::string manifestPath = std::string(root) + "/" + kOrdinaryManifestName;
  const bool present = Storage.exists(manifestPath.c_str());
  if (!present && !allowMissingManifest) return false;
  if (!present) {
    HalFile dir = Storage.open(root, O_RDONLY);
    if (!dir.isOpen() || !dir.isDirectory()) {
      if (dir.isOpen()) (void)dir.close();
      return false;
    }
    HalFile entry = dir.openNextFile();
    const bool empty = !entry.isOpen();
    if (entry.isOpen()) (void)entry.close();
    const bool closed = dir.close();
    return empty && closed && Storage.rmdir(root);
  }
  for (size_t i = 0; i < plan.entryCount; ++i) {
    const std::string path = std::string(root) + "/" + plan.entries[i].name;
    if (Storage.exists(path.c_str()) && !Storage.remove(path.c_str())) return false;
  }
  return Storage.remove(manifestPath.c_str()) && Storage.rmdir(root);
}

class SdSource {
 public:
  explicit SdSource(const char* root) : root_(root) {}
  bool entrySize(const char* name, uint64_t& size) {
    return fileSize(root_ + "/" + name, size);
  }
  bool readAt(const char* name, uint64_t at, uint8_t* buffer, size_t count) {
    return readExact(root_ + "/" + name, at, buffer, count);
  }
 private:
  std::string root_;
};

class SdHash {
 public:
  SdHash() { mbedtls_sha256_init(&hash_); }
  ~SdHash() { mbedtls_sha256_free(&hash_); }
  bool start() { return mbedtls_sha256_starts_ret(&hash_, 0) == 0; }
  bool update(const uint8_t* data, size_t size) {
    return mbedtls_sha256_update_ret(&hash_, data, size) == 0;
  }
  bool finish(uint8_t output[32]) {
    return mbedtls_sha256_finish_ret(&hash_, output) == 0;
  }
 private:
  mbedtls_sha256_context hash_{};
};

class SdDirectory {
 public:
  explicit SdDirectory(const char* path) : root_(path) {}
  bool readManifest(char* output, size_t capacity, size_t& length) {
    if (!output || capacity < kManifestBytes) return false;
    char buffer[kManifestBytes]{};
    if (!manifestBytes(root_.c_str(), buffer, length)) return false;
    std::memcpy(output, buffer, length);
    return true;
  }
  bool exactEntries(const OrdinaryPackagePlan& plan) {
    return inventory(root_.c_str(), plan, true);
  }
  bool entrySize(const char* name, uint64_t& size) {
    return fileSize(root_ + "/" + name, size);
  }
  bool readAt(const char* name, uint64_t at, uint8_t* output, size_t count) {
    return readExact(root_ + "/" + name, at, output, count);
  }
 private:
  std::string root_;
};

class SdStage {
 public:
  ~SdStage() {
    if (writer_.isOpen()) (void)writer_.close();
    // Do NOT remove a retained power-cut stage on destruction.
  }
  bool begin(const OrdinaryPackagePlan& plan) {
    OrdinaryTransactionPaths paths{};
    if (owns_ || !ordinaryTransactionPaths(plan.identity.kind,
                                            plan.identity.id, paths)) return false;
    const std::string root = std::string(paths.target).substr(0,
        std::string(paths.target).find_last_of('/'));
    if ((!Storage.exists(root.c_str()) && !Storage.mkdir(root.c_str(), false)) ||
        !storageDirectory(root.c_str()) || Storage.exists(paths.stage) ||
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
  bool append(const uint8_t* bytes, size_t size) {
    return writer_.isOpen() && writer_.write(bytes, size) == size;
  }
  bool endEntry() { return writer_.isOpen() && writer_.close(); }
  bool readEntry(const char* name, uint64_t offset, uint8_t* bytes, size_t size) {
    return owns_ && readExact(root_ + "/" + name, offset, bytes, size);
  }
  bool writeManifest(const uint8_t* bytes, size_t size) {
    if (!owns_ || !bytes || !size || size > kManifestBytes || writer_.isOpen()) return false;
    const std::string path = root_ + "/" + kOrdinaryManifestName;
    HalFile manifest = Storage.open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL);
    if (!manifest.isOpen() || manifest.isDirectory()) {
      if (manifest.isOpen()) (void)manifest.close();
      return false;
    }
    const bool written = manifest.write(bytes, size) == size;
    return manifest.close() && written;
  }
  bool seal() {
    if (!owns_ || writer_.isOpen()) return false;
    SdDirectory directory(root_.c_str());
    char bytes[kManifestBytes]{};
    size_t length = 0;
    return directory.readManifest(bytes, sizeof(bytes), length) &&
           inventory(root_.c_str(), plan_, true);
  }
  bool discard() {
    if (!owns_) return false;
    if (writer_.isOpen() && !writer_.close()) return false;
    if (!purgeKnown(root_.c_str(), plan_, true)) return false;
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

bool verifyManaged(const char* path, const PackageRuntimePolicy& policy,
                   uint32_t (*resolve)(const char*), Identity& observed) {
  if (!path || !resolve || !storageDirectory(path)) return false;
  SdDirectory directory(path);
  SdHash hash;
  uint8_t io[kOrdinaryIoBytes]{};
  return verifyCanonicalOrdinaryDirectory(directory, hash, resolve, policy,
                                          io, observed);
}

bool purgeManaged(const char* path, Kind kind, const char* expectedId) {
  if (!path || !safeId(expectedId) || !storageDirectory(path)) return false;
  const std::string manifestPath = std::string(path) + "/" + kOrdinaryManifestName;
  if (!Storage.exists(manifestPath.c_str())) {
    // The manifest is deleted only after every entry. No manifest + nonempty
    // directory is ambiguous and cannot be purged.
    OrdinaryPackagePlan empty{};
    return purgeKnown(path, empty, true);
  }
  char bytes[kManifestBytes]{};
  size_t length = 0;
  OrdinaryPackagePlan plan{};
  if (!manifestBytes(path, bytes, length) ||
      !parseOrdinaryManifest(bytes, length, plan) ||
      plan.identity.kind != kind || std::strcmp(plan.identity.id, expectedId))
    return false;
  return purgeKnown(path, plan, false);
}
} // namespace

bool verifyOrdinarySdDirectory(const char* managedDirectory,
    const PackageRuntimePolicy& policy,
    uint32_t (*resolveCapability)(const char*), Identity& observed) {
  observed = {};
  return Storage.ready() && safeSourcePath(managedDirectory) &&
      verifyManaged(managedDirectory, policy, resolveCapability, observed);
}

OrdinaryInstallOutcome installOrdinaryFromSd(
    const char* sourceDirectory, const PackageRuntimePolicy& policy,
    uint32_t (*resolveCapability)(const char*)) {
  OrdinaryInstallOutcome invalid{};
  if (!Storage.ready() || !safeSourcePath(sourceDirectory) ||
      !resolveCapability || !storageDirectory(sourceDirectory)) return invalid;
  char metadata[kManifestBytes]{};
  size_t length = 0;
  OrdinaryPackagePlan plan{};
  if (!manifestBytes(sourceDirectory, metadata, length) ||
      !parseOrdinaryManifest(metadata, length, plan) ||
      !inventory(sourceDirectory, plan, true)) return invalid;
  SdSource source(sourceDirectory);
  SdStage destination;
  SdHash hash;
  Ops ops;
  uint8_t io[kOrdinaryIoBytes]{};
  const auto verify = [&policy, resolveCapability](const char* path, Identity& observed) {
    return verifyManaged(path, policy, resolveCapability, observed);
  };
  const Kind kind = plan.identity.kind;
  const std::string id = plan.identity.id;
  const auto purge = [kind, id](const char* path) {
    return purgeManaged(path, kind, id.c_str());
  };
  // No other component is permitted to use this entrypoint without the
  // per-identity manager mutation lock; publication also acquires its own
  // exclusive mapping replacement lease and re-verifies the candidate.
  return installCanonicalOrdinaryPackage(metadata, length, source, destination,
      hash, resolveCapability, policy, io, ops, verify, purge, true);
}

} // namespace RuntimePackages
