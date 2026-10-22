#include "NativeAppSource.h"
#include "ManagedAppAdmission.h"
#include <HalStorage.h>
#include <cstring>
#include <string>
#include <mutex>
#include <fcntl.h>
#include "runtime/resources/ExecutionContext.h"
#if defined(BOARD_XTEINK_X4_PRO) || defined(BOARD_T5S3_PRO)
#include <NativeAppVolumeVfs.h>
#endif
extern "C" bool esp_elf_admit_managed_app(const char*, const uint8_t*, size_t);
namespace {
struct Source {
  StorageSourceLease lease{};
  std::string path;
  bool active = false, registered = false, retained = false;
};
Source source;
std::mutex sourceMutex;
uint32_t invocation() {
  auto* context = RuntimeResources::ExecutionContext::current();
  return context && context->running(context->id()) ? context->id() : 0;
}
bool matches(const char* path, bool requireOwner) {
  return source.active && !source.retained && path && source.path == path &&
      Storage.matchesNativeSource(source.lease) &&
      (!requireOwner || (invocation() && (!source.lease.volume || source.lease.owner == invocation())));
}
bool sameProof(const StorageSourceLease& a, const StorageSourceLease& b) {
  return a.volume == b.volume && a.context == b.context && a.epoch == b.epoch &&
      a.token == b.token && a.owner == b.owner;
}
}
extern "C" bool native_app_source_begin(const char* path) {
  if (!path || std::strncmp(path, "/sd/", 4) || !path[4]) return false;
  std::lock_guard<std::mutex> lock(sourceMutex);
  if (source.active || source.retained) return false;
  StorageSourceLease lease{};
  if (!Storage.captureNativeSource(lease)) return false;
  source.lease = lease;
  source.path = path;
  source.active = true;
  return true;
}
extern "C" bool native_app_source_bind_owner() {
  std::lock_guard<std::mutex> lock(sourceMutex);
  const uint32_t owner = invocation();
  return owner && matches(source.path.c_str(), false) && Storage.bindNativeSourceOwner(source.lease, owner);
}
extern "C" int native_app_source_kind(const char* path) {
  std::lock_guard<std::mutex> lock(sourceMutex);
#if defined(BOARD_XTEINK_X4_PRO) || defined(BOARD_T5S3_PRO)
  return matches(path, true) && source.lease.volume ? 1 : -1;
#else
  return !source.active || matches(path, true) ? 0 : -1;
#endif
}
namespace RuntimePackages {
bool captureNativeAppSource(const char* path, StorageSourceLease& proof) {
  proof = {};
  std::lock_guard<std::mutex> lock(sourceMutex);
#if !defined(BOARD_XTEINK_X4_PRO) && !defined(BOARD_T5S3_PRO)
  if (!source.active) return true; // Preserve direct legacy admission callers.
#endif
  if (!matches(path, true)) return false;
  proof = source.lease;
  return true;
}
bool matchesNativeAppSource(const char* path, const StorageSourceLease& proof) {
  StorageSourceLease observed{};
  return captureNativeAppSource(path, observed) && sameProof(observed, proof);
}
}
extern "C" int native_app_source_open(const char* path) {
  std::lock_guard<std::mutex> lock(sourceMutex);
#if defined(BOARD_XTEINK_X4_PRO) || defined(BOARD_T5S3_PRO)
  if (!matches(path, true) || !source.lease.volume || source.registered) return -1;
  if (native_app_register_volume_vfs(source.lease.volume) != ESP_OK) return -1;
  source.registered = true; // Failed open may retain an unreturned provider token.
  return native_app_open_volume_file(source.lease.volume, path + 3, O_RDONLY);
#else
  (void)path;
  return -1;
#endif
}
extern "C" bool native_app_source_admit(void* opaque, const uint8_t* bytes, size_t size) {
  const auto* path = static_cast<const char*>(opaque);
  if (native_app_source_kind(path) != 1) return false;
  std::shared_ptr<const std::string> captured, observed;
  const auto state = RuntimePackages::captureManagedAppSidecar(path, captured);
  if (state != RuntimePackages::ManagedAppMetadata::Captured &&
      state != RuntimePackages::ManagedAppMetadata::CapturedLegacy) return false;
  return esp_elf_admit_managed_app(path, bytes, size) && native_app_source_kind(path) == 1 &&
      RuntimePackages::captureManagedAppSidecar(path, observed) == state && observed == captured;
}
extern "C" bool native_app_source_finish_io() {
  std::lock_guard<std::mutex> lock(sourceMutex);
  if (source.retained) return false;
  if (source.active && source.lease.volume && !Storage.matchesNativeSource(source.lease)) {
    source.retained = true;
    return false;
  }
#if defined(BOARD_XTEINK_X4_PRO) || defined(BOARD_T5S3_PRO)
  if (source.registered) {
    // Synchronous owner: every libc call, including close, has returned.
    if (!Storage.matchesNativeSource(source.lease) ||
        native_app_unregister_volume_vfs(source.lease.volume) != ESP_OK) {
      source.retained = true;
      return false;
    }
    source.registered = false;
  }
#endif
  return true;
}
extern "C" bool native_app_source_end() {
  std::lock_guard<std::mutex> lock(sourceMutex);
  if (!source.active) return !source.retained;
  if (source.registered || source.retained || !Storage.releaseNativeSource(source.lease)) {
    source.retained = true;
    return false;
  }
  source = {};
  return true;
}
extern "C" bool native_app_source_retained() {
  std::lock_guard<std::mutex> lock(sourceMutex);
  return source.retained;
}
extern "C" void native_app_source_retain() {
  std::lock_guard<std::mutex> lock(sourceMutex);
  if (source.active && source.lease.volume) source.retained = true;
}
