#include "DriverStageActions.h"

#include "DriverPackage.h"

#include <HalStorage.h>
#include <NativeAppLauncher.h>

#include <cstring>
#include <string>

namespace RuntimeDrivers {
namespace {
constexpr size_t kManifestLimit = 4096;

struct Ops {
  bool exists(const char* path) const { return Storage.exists(path); }
  bool rename(const char* from, const char* to) const {
    return Storage.rename(from, to);
  }
};

// A partially written stage can contain either/both of exactly these files.
// No nested directories, additional files or duplicate directory entries are
// permitted. Unknown user data must NEVER be deleted by a recovery operation.
bool knownStageEntries(const char* path, bool requireComplete) {
  HalFile directory = Storage.open(path, O_RDONLY);
  if (!directory.isOpen() || !directory.isDirectory()) {
    if (directory.isOpen()) (void)directory.close();
    return false;
  }
  bool elf = false, manifest = false, okay = true;
  while (okay) {
    HalFile entry = directory.openNextFile();
    if (!entry.isOpen()) break;
    char name[128]{};
    const size_t length = entry.getName(name, sizeof(name));
    if (!length || length >= sizeof(name) || entry.isDirectory()) {
      okay = false;
    } else if (std::strcmp(name, "driver.elf") == 0 && !elf) {
      elf = true;
    } else if (std::strcmp(name, "manifest.json") == 0 && !manifest) {
      manifest = true;
    } else {
      okay = false;
    }
    (void)entry.close();
  }
  const bool closed = directory.close();
  return closed && okay && (!requireComplete || (elf && manifest));
}

bool readStageManifest(const char* path, std::string& json) {
  json.clear();
  const std::string name = std::string(path) + "/manifest.json";
  HalFile file = Storage.open(name.c_str(), O_RDONLY);
  if (!file.isOpen() || file.isDirectory()) {
    if (file.isOpen()) (void)file.close();
    return false;
  }
  const uint64_t length = file.fileSize64();
  if (!length || length > kManifestLimit) {
    (void)file.close();
    return false;
  }
  json.resize(static_cast<size_t>(length));
  const int count = file.read(&json[0], json.size());
  const bool closed = file.close();
  if (count != static_cast<int>(length) || !closed) {
    json.clear();
    return false;
  }
  return true;
}

bool verifyDirectory(const char* path, const char* id,
                     RuntimePackages::Identity& observed) {
  observed = {};
  if (!knownStageEntries(path, true)) return false;
  std::string json;
  if (!readStageManifest(path, json)) return false;
  DriverPackageInfo info{};
  const std::string elf = std::string("/sd") + path + "/driver.elf";
  if (!validateDriverPayload(json, elf.c_str(), &info) ||
      std::strcmp(info.id, id) != 0) return false;
  return RuntimePackages::makeIdentity(RuntimePackages::Kind::Driver,
      info.id, info.version, "driver.elf", false, &observed);
}

bool purgeKnownStage(const char* path) {
  if (!knownStageEntries(path, false)) return false;
  // Manifest goes last, leaving its identity available if payload deletion is
  // interrupted. Never recurse or delete directories containing other files.
  const std::string elf = std::string(path) + "/driver.elf";
  const std::string manifest = std::string(path) + "/manifest.json";
  if (Storage.exists(elf.c_str()) && !Storage.remove(elf.c_str())) return false;
  if (Storage.exists(manifest.c_str()) && !Storage.remove(manifest.c_str())) return false;
  return Storage.rmdir(path);
}

bool validDriverRoot(const char* id,
                     RuntimePackages::OrdinaryTransactionPaths& paths) {
  return Storage.ready() &&
      RuntimePackages::ordinaryTransactionPaths(RuntimePackages::Kind::Driver,
                                                id, paths) &&
      native_app_register_sd_vfs() == ESP_OK;
}

bool legacyPending(const char* id) {
  const std::string prefix = std::string("/Drivers/.") + id;
  return Storage.exists((prefix + ".previous").c_str()) ||
         Storage.exists((prefix + ".install").c_str());
}
}  // namespace

bool inspectDriverStage(const char* id,
                        RuntimePackages::OrdinaryStageReview& review) {
  review = {};
  RuntimePackages::OrdinaryTransactionPaths paths{};
  if (!validDriverRoot(id, paths)) return false;
  if (legacyPending(id)) {
    review.state = RuntimePackages::OrdinaryStageState::RecoveryRequired;
    return true;
  }
  Ops ops;
  review = RuntimePackages::reviewOrdinaryStage(ops,
      RuntimePackages::Kind::Driver, id,
      [id](const char* path, RuntimePackages::Identity& identity) {
        return verifyDirectory(path, id, identity);
      });
  return true;
}

RuntimePackages::OrdinaryTransactionResult retryDriverStage(const char* id) {
  RuntimePackages::OrdinaryTransactionPaths paths{};
  if (!validDriverRoot(id, paths))
    return RuntimePackages::OrdinaryTransactionResult::InvalidIdentity;
  if (legacyPending(id))
    return RuntimePackages::OrdinaryTransactionResult::AmbiguousState;
  Ops ops;
  RuntimePackages::Identity observed{};
  return RuntimePackages::retryOrdinaryStage(ops,
      RuntimePackages::Kind::Driver, id,
      [id](const char* path, RuntimePackages::Identity& identity) {
        return verifyDirectory(path, id, identity);
      },
      [](const char* path) { return purgeKnownStage(path); }, observed);
}

RuntimePackages::OrdinaryStageDiscardResult discardDriverStage(const char* id) {
  RuntimePackages::OrdinaryTransactionPaths paths{};
  if (!validDriverRoot(id, paths))
    return RuntimePackages::OrdinaryStageDiscardResult::InvalidIdentity;
  if (legacyPending(id))
    return RuntimePackages::OrdinaryStageDiscardResult::RecoveryRequired;
  Ops ops;
  return RuntimePackages::discardOrdinaryStage(ops,
      RuntimePackages::Kind::Driver, id,
      [](const char* path) { return knownStageEntries(path, false); },
      [](const char* path) { return purgeKnownStage(path); });
}

}  // namespace RuntimeDrivers
