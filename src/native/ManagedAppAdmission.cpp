#include "ManagedAppAdmission.h"

#include <HalStorage.h>

#include <cstring>
#include <mutex>
#include <new>

#include "runtime/packages/PackageExecutableAdmission.h"
#include "runtime/packages/PackageOrdinaryManifest.h"
#include "runtime/packages/PackageOrdinaryTransaction.h"
#include "runtime/packages/PackageVerificationReceipt.h"
#include "runtime/resources/ExecutionContext.h"

namespace RuntimePackages {
namespace {
bool managedPath(const char* path) { return path && !std::strncmp(path, "/sd/Apps/", 9) && std::strchr(path + 9, '/'); }
uint32_t invocation() {
  auto* context = RuntimeResources::ExecutionContext::current();
  return context && context->running(context->id()) ? context->id() : 0;
}
struct Admission {
  Identity identity{};
  char root[96]{};
  std::string path, sidecar;
  uint8_t manifestDigest[32]{}, executableDigest[32]{};
  StorageGenerationStamp sourceStamp{};
  uint32_t owner = 0;
  bool closeUncertain = false;
  bool legacy = false;
  AppIntegrity legacyIntegrity{};
  ~Admission() {
    if (root[0] && !closeUncertain) (void)systemPackageUseGate().unpin(root);
  }
};
std::shared_ptr<Admission> current;
std::mutex admissionMutex;
bool readMetadata(const std::string& path, size_t maximum, std::string& out, bool& uncertain) {
  out.clear();
#if defined(BOARD_XTEINK_X4_PRO) || defined(BOARD_T5S3_PRO)
  HalFile file;
  if (!Storage.openFileForRead("APP", path.c_str(), file)) return false;
#else
  HalFile file = Storage.open(path.c_str(), O_RDONLY);
#endif
  if (!file.isOpen()) return false;
  const uint64_t size = !file.isDirectory() ? file.fileSize64() : 0;
  std::unique_ptr<char[]> bytes(size && size <= maximum ? new (std::nothrow) char[static_cast<size_t>(size)] : nullptr);
  const bool read = bytes && file.read(bytes.get(), static_cast<size_t>(size)) == static_cast<int>(size);
  const bool closed = file.close();
  if (!closed) uncertain = true;
  if (!read || !closed) return false;
  out.assign(bytes.get(), static_cast<size_t>(size));
  return true;
}
std::shared_ptr<Admission> capture(const char* path) {
  const uint32_t owner = invocation();
  std::lock_guard<std::mutex> lock(admissionMutex);
  return current && owner && current->owner == owner && path && current->path == path ? current : nullptr;
}
}  // namespace
bool beginManagedAppAdmission(const Identity& identity, const char* sdPath) {
  Identity canonical{};
  OrdinaryTransactionPaths paths{};
  const uint32_t owner = invocation();
  if (!owner || identity.kind != Kind::Application || !canonicalIdentity(identity, &canonical) ||
      resourceOnly(identity) || !managedPath(sdPath) || !ordinaryTransactionPaths(identity.kind, identity.id, paths) ||
      !systemPackageUseGate().pinned(paths.target))
    return false;
  const std::string expected = std::string("/sd") + paths.target + "/" + identity.artifact;
  if (expected != sdPath) return false;
  {
    std::lock_guard<std::mutex> lock(admissionMutex);
    if (current) return false;
  }
  // Refresh the independent SdFat view after a completed raw-FS session when
  // there are no live handles/owners. Refusal keeps the existing cold path;
  // it never manufactures a quiescent stamp or resets retained ownership.
  (void)Storage.reconcileExternalStorage();
  if (!Storage.ready()) return false;
  std::shared_ptr<Admission> candidate(new (std::nothrow) Admission());
  if (!candidate || !systemPackageUseGate().pin(paths.target)) return false;
  std::strcpy(candidate->root, paths.target);
  candidate->identity = identity;
  candidate->path = sdPath;
  candidate->owner = owner;
  candidate->sourceStamp = Storage.generation();
  std::string manifest;
  std::unique_ptr<OrdinaryPackagePlan> plan(new (std::nothrow) OrdinaryPackagePlan{});
  if (!plan || !readMetadata(std::string(paths.target) + "/.package.json", 4096, manifest, candidate->closeUncertain) ||
      !parseOrdinaryManifest(manifest.data(), manifest.size(), *plan) || !samePackage(identity, plan->identity) ||
      std::strcmp(identity.version, plan->identity.version) ||
      std::strcmp(identity.artifact, plan->identity.artifact) || resourceOnly(plan->identity))
    return false;
  constexpr PackageRuntimePolicy policy{"xtensa-esp32s3", 2, 8u * 1024u * 1024u, 16u * 1024u * 1024u};
  if (!preflightCapturedPackage(*plan, policy) ||
      !packageSnapshotDigest(reinterpret_cast<const uint8_t*>(manifest.data()), manifest.size(),
                             candidate->manifestDigest))
    return false;
  bool executable = false;
  for (size_t i = 0; i < plan->entryCount; ++i)
    if (plan->entries[i].executable) {
      if (executable || std::strcmp(plan->entries[i].name, identity.artifact) ||
          !receiptDigest(plan->entries[i].sha256, candidate->executableDigest))
        return false;
      executable = true;
    }
  if (!executable) return false;
  const std::string artifact(identity.artifact);
  const std::string sidecar = artifact.substr(0, artifact.size() - 4) + ".json";
  if (!readMetadata(std::string(paths.target) + "/" + sidecar, 2048, candidate->sidecar, candidate->closeUncertain) ||
      !declaredPackageSnapshot(*plan, sidecar.c_str(), reinterpret_cast<const uint8_t*>(candidate->sidecar.data()),
                               candidate->sidecar.size()))
    return false;
  // Dependency and optional-capability code still applies its existing parser
  // and authorization, but consumes this exact verified sidecar snapshot.
  std::lock_guard<std::mutex> lock(admissionMutex);
  if (current || invocation() != owner) return false;
  current = std::move(candidate);
  return true;
}
bool beginLooseAppAdmission(const char* sdPath, LegacyAppSidecarValidator validator) {
  const uint32_t owner = invocation();
  if (!owner || !sdPath || std::strncmp(sdPath, "/sd/", 4) || managedPath(sdPath) || !validator) return false;
  const std::string path(sdPath);
  if (path.size() < 8 || path.compare(path.size() - 4, 4, ".elf")) return false;
  {
    std::lock_guard<std::mutex> lock(admissionMutex);
    if (current) return false;
  }
  (void)Storage.reconcileExternalStorage();
  if (!Storage.ready()) return false;
  std::shared_ptr<Admission> candidate(new (std::nothrow) Admission());
  if (!candidate) return false;
  candidate->legacy = true;
  candidate->identity.legacyVersion = true;
  candidate->path = path;
  candidate->owner = owner;
  candidate->sourceStamp = Storage.generation();
  const std::string sidecar = path.substr(3, path.size() - 3 - 4) + ".json";
  const bool present = Storage.exists(sidecar.c_str());
  const auto observed = Storage.generation();
  if (!present &&
      (observed.mount != candidate->sourceStamp.mount || observed.mutation != candidate->sourceStamp.mutation))
    return false;
  if (present) {
    if (!readMetadata(sidecar, 2048, candidate->sidecar, candidate->closeUncertain) ||
        !validator(candidate->sidecar, path.substr(path.find_last_of('/') + 1), candidate->legacyIntegrity))
      return false;
    if (candidate->legacyIntegrity.present &&
        (candidate->legacyIntegrity.sizeBytes < 52 || candidate->legacyIntegrity.sizeBytes > 8u * 1024u * 1024u ||
         !receiptDigest(candidate->legacyIntegrity.sha256, candidate->executableDigest)))
      return false;
  }
  if (!candidate->sidecar.empty() && !packageSnapshotDigest(reinterpret_cast<const uint8_t*>(candidate->sidecar.data()),
                                                            candidate->sidecar.size(), candidate->manifestDigest))
    return false;
  std::lock_guard<std::mutex> lock(admissionMutex);
  if (current || invocation() != owner) return false;
  current = std::move(candidate);
  return true;
}
void endManagedAppAdmission() {
  std::shared_ptr<Admission> retired;
  {
    std::lock_guard<std::mutex> lock(admissionMutex);
    retired = std::move(current);
  }
}
ManagedAppMetadata captureManagedAppSidecar(const char* sdPath, std::shared_ptr<const std::string>& sidecar,
                                            Identity* identity) {
  sidecar.reset();
  if (identity) *identity = {};
  auto active = capture(sdPath);
  if (!active) {
    std::lock_guard<std::mutex> lock(admissionMutex);
    return managedPath(sdPath) || (current && sdPath && current->path == sdPath) ? ManagedAppMetadata::Denied
                                                                                 : ManagedAppMetadata::Unmanaged;
  }
  sidecar = std::shared_ptr<const std::string>(active, &active->sidecar);
  if (identity) *identity = active->identity;
  return active->legacy ? ManagedAppMetadata::CapturedLegacy : ManagedAppMetadata::Captured;
}
}  // namespace RuntimePackages

// Private loader callback, deliberately absent from ELF import registration.
extern "C" bool esp_elf_admit_managed_app(const char* path, const uint8_t* bytes, size_t size) {
  using namespace RuntimePackages;
  auto active = capture(path);
  if (!active) {
    std::lock_guard<std::mutex> lock(admissionMutex);
    return !managedPath(path) && !(current && path && current->path == path);
  }
  if (active->legacy) {
    if (active->legacyIntegrity.present &&
        (size != active->legacyIntegrity.sizeBytes ||
         !admitLooseExecutableSnapshot(path, active->manifestDigest, active->executableDigest, bytes, size,
                                       active->sourceStamp)))
      return false;
  } else if (!admitInstalledExecutableSnapshot(active->identity, active->manifestDigest, active->executableDigest,
                                               bytes, size, active->sourceStamp))
    return false;
  return capture(path) == active;  // Revoke/replacement during IO cannot admit late work.
}
