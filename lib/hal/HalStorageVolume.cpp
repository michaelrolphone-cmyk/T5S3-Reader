// Generic storage.volume backend. X4 selects it instead of the legacy SdFat
// backend; no board pins, FAT internals, or device protocol live in this adapter.
#if defined(BOARD_XTEINK_X4_PRO) || defined(BOARD_T5S3_PRO)
#define HAL_STORAGE_IMPL
#include "HalStorage.h"
#include <Arduino.h>
#include <Logging.h>
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstring>
#include <limits>

namespace {
const risc_storage_volume_api_v1 *volume;
const risc_storage_volume_api_v1_ext *extended;
StorageGenerationTracker generations;
bool mediaReady() { return volume && volume->ready(volume->context); }
bool writable(oflag_t flags) { return (flags & (O_WRONLY | O_RDWR)) != 0; }
constexpr size_t kMaxOperationBytes = 16u * 1024u * 1024u;
constexpr uint32_t kOperationMs = 20000;
}
HalStorage HalStorage::instance;
HalStorage::HalStorage() {
  storageMutex = xSemaphoreCreateMutex();
  assert(storageMutex);
}
class HalStorage::StorageLock {
  bool held;
 public:
  StorageLock() : held(xSemaphoreTake(Storage.storageMutex, pdMS_TO_TICKS(kOperationMs)) == pdTRUE) {}
  ~StorageLock() { if (held) xSemaphoreGive(Storage.storageMutex); }
  explicit operator bool() const { return held; }
  bool initialized() const { return held && Storage.initialized; }
};
bool HalStorage::bindVolume(const risc_storage_volume_api_v1 *api) {
  StorageLock lock;
  const auto *ext = risc_storage_volume_extension(api);
  if (!lock || !ext || !api->ready || !api->stat || !api->dir_open || !api->dir_next ||
      !api->file_read || !api->file_write || !api->file_close || !api->remove ||
      !ext->file_open || !ext->file_seek || !ext->file_info || !ext->file_sync ||
      !ext->dir_rewind || !ext->dir_close_checked || !ext->handle_error ||
      !ext->mkdir || !ext->rename || !generations.mountAttempt()) return false;
  volume = api; extended = ext; initialized = mediaReady();
  generations.mounted(initialized);
  return initialized;
}
bool HalStorage::begin() {
  StorageLock lock;
  if (!lock || !volume || !volume->refresh || !generations.mountAttempt()) return false;
  initialized = volume->refresh(volume->context) && mediaReady();
  generations.mounted(initialized); return initialized;
}
bool HalStorage::ready() const { StorageLock lock; return lock && initialized && mediaReady(); }
StorageGenerationStamp HalStorage::generation() const {
  StorageLock lock; return lock ? generations.stamp(initialized && mediaReady()) : StorageGenerationStamp{};
}
bool HalStorage::unchanged(const StorageGenerationStamp& stamp) const { return stamp.matches(generation()); }
void HalStorage::invalidateObservations() { StorageLock lock; if (lock) generations.mutationAttempt(); }
void HalStorage::externalStorageBegin() { StorageLock lock; if (lock) generations.externalBegin(); }
void HalStorage::externalStorageEnd(bool closed) { StorageLock lock; if (lock) generations.externalEnd(closed); }
void HalStorage::externalStorageUncertain() { StorageLock lock; if (lock) generations.externalUncertain(); }
bool HalStorage::reconcileExternalStorage() {
  { StorageLock lock; if (!lock) return false; if (!generations.needsReconcile()) return initialized && mediaReady(); }
  return begin();
}
void HalStorage::markUnavailable() { StorageLock lock; if (lock) { initialized = false; generations.mutationAttempt(); } }
bool HalStorage::prepareForSleep() {
  StorageLock lock;
  if (!lock) return false;
  if (!volume) return !initialized; // Early timer wake has never mounted storage.
  const auto* power = risc_storage_volume_power(volume);
  return power && power->prepare_power_down && power->prepare_power_down(volume->context);
}
bool HalStorage::cancelSleep() {
  StorageLock lock;
  if (!lock) return false;
  if (!volume) return !initialized;
  const auto* power = risc_storage_volume_power(volume);
  return power && power->cancel_power_down && power->cancel_power_down(volume->context);
}
bool HalStorage::commitSleep() {
  StorageLock lock;
  if (!lock) return false;
  if (!volume) return !initialized; // Display-only timer boot: bootstrap is off.
  const auto* power = risc_storage_volume_power_commit(volume);
  return power && power->commit_power_down(volume->context);
}
bool halStorageCommitSleep() { return Storage.commitSleep(); }
bool halStoragePrepareForSleep() { return Storage.prepareForSleep(); }
bool halStorageCancelSleep() { return Storage.cancelSleep(); }
void halStorageMediaUnavailable() { Storage.markUnavailable(); }

class HalFile::Impl {
 public:
  uint32_t handle = 0;
  bool directory = false, writer = false, tracked = false, syncWrites = false;
  uint8_t error = 0;
  std::string path;
  Impl(uint32_t id, bool dir, bool write, const char *name)
      : handle(id), directory(dir), writer(write), path(name) {
    tracked = generations.opened(writer);
  }
  bool closeLocked() {
    if (!handle) return true;
    if (writer) generations.mutationAttempt();
    const bool ok = directory ? extended->dir_close_checked(volume->context, handle)
                              : volume->file_close(volume->context, handle, true);
    if (!ok) { error = 1; generations.externalUncertain(); return false; }
    handle = 0;
    if (tracked) { generations.closed(writer); tracked = false; }
    return true;
  }
  bool info(uint64_t& size, uint64_t& offset) const {
    return handle && !directory && mediaReady() && extended->file_info(volume->context, handle, &size, &offset);
  }
};
HalFile::HalFile() = default;
HalFile::HalFile(std::unique_ptr<Impl> value) : impl(std::move(value)) {}
HalFile::HalFile(HalFile&&) = default;
HalFile::~HalFile() {
  if (!impl) return;
  HalStorage::StorageLock lock;
  if (lock) (void)impl->closeLocked();
  // A failed close retains its provider slot and poisons generation reuse.
  // The provider refuses remount/quiesce while that slot remains live.
}
HalFile& HalFile::operator=(HalFile&& other) {
  if (this != &other) {
    if (impl) { HalStorage::StorageLock lock; if (!lock || !impl->closeLocked()) return *this; }
    impl = std::move(other.impl);
  }
  return *this;
}
HalFile HalStorage::open(const char *path, oflag_t flags) {
  StorageLock lock;
  if (!lock || !initialized || !mediaReady() || !path) return {};
  uint64_t size = 0; bool directory = false;
  const bool found = volume->stat(volume->context, path, &size, &directory);
  if (found && directory) {
    if (flags != O_RDONLY) return {};
    const uint32_t handle = volume->dir_open(volume->context, path);
    return handle ? HalFile(std::make_unique<HalFile::Impl>(handle, true, false, path)) : HalFile{};
  }
  const bool write = writable(flags);
  if (write) generations.mutationAttempt();
  uint32_t mode = ((flags & O_RDWR) || !write) ? RISC_STORAGE_OPEN_READ : 0;
  if (write) mode |= RISC_STORAGE_OPEN_WRITE;
  if (flags & O_CREAT) mode |= RISC_STORAGE_OPEN_CREATE;
  if (flags & O_TRUNC) mode |= RISC_STORAGE_OPEN_TRUNCATE;
  if (flags & O_EXCL) mode |= RISC_STORAGE_OPEN_EXCLUSIVE;
  if (flags & O_APPEND) mode |= RISC_STORAGE_OPEN_APPEND;
  const uint32_t handle = extended->file_open(volume->context, path, mode);
  if (!handle) return {};
  auto impl = std::make_unique<HalFile::Impl>(handle, false, write, path);
  impl->syncWrites = (flags & O_SYNC) != 0;
  if (flags & O_AT_END) {
    uint64_t bytes = 0, offset = 0;
    if (!impl->info(bytes, offset) || !extended->file_seek(volume->context, handle, bytes)) {
      (void)impl->closeLocked(); return {};
    }
  }
  return HalFile(std::move(impl));
}
bool HalStorage::exists(const char *path) {
  StorageLock lock; uint64_t size = 0; bool dir = false;
  return lock && initialized && mediaReady() && path && volume->stat(volume->context, path, &size, &dir);
}
bool HalStorage::mkdir(const char *path, bool parents) {
  if (!path || path[0] != '/' || std::strlen(path) >= RISC_STORAGE_VOLUME_PATH_MAX) return false;
  // Validate all components before creating any parent directories.
  for (const char *start = path + 1; *start;) {
    const char *end = std::strchr(start, '/');
    if (!end) end = start + std::strlen(start);
    const size_t n = end - start;
    if (!n || (n == 1 && start[0] == '.') || (n == 2 && start[0] == '.' && start[1] == '.') ||
        start[n - 1] == '.' || start[n - 1] == ' ') return false;
    for (const char *p = start; p < end; ++p)
      if (static_cast<unsigned char>(*p) < 32 || std::strchr(":\\*?\"<>|", *p)) return false;
    if (*end && !end[1]) return false;
    start = *end ? end + 1 : end;
  }
  StorageLock lock; if (!lock || !initialized || !mediaReady()) return false;
  generations.mutationAttempt();
  char prefix[RISC_STORAGE_VOLUME_PATH_MAX]; std::strcpy(prefix, path);
  const size_t length = std::strlen(prefix);
  const uint32_t began = millis();
  for (size_t i = 1; i <= length; ++i) {
    if (i != length && (!parents || prefix[i] != '/')) continue;
    if (millis() - began >= kOperationMs) return false;
    const char saved = prefix[i]; prefix[i] = 0;
    uint64_t size = 0; bool dir = false;
    const bool found = volume->stat(volume->context, prefix, &size, &dir);
    const bool ok = found ? dir : extended->mkdir(volume->context, prefix);
    prefix[i] = saved;
    if (!ok) return false;
    delay(1);
  }
  return true;
}
bool HalStorage::ensureDirectoryExists(const char *path) { return mkdir(path, true); }
bool HalStorage::remove(const char *path) {
  StorageLock lock; if (!lock || !initialized || !mediaReady()) return false;
  uint64_t size = 0; bool dir = false;
  if (!volume->stat(volume->context, path, &size, &dir) || dir) return false;
  generations.mutationAttempt(); return volume->remove(volume->context, path);
}
bool HalStorage::rmdir(const char *path) {
  StorageLock lock; if (!lock || !initialized || !mediaReady()) return false;
  uint64_t size = 0; bool dir = false;
  if (!volume->stat(volume->context, path, &size, &dir) || !dir) return false;
  generations.mutationAttempt(); return volume->remove(volume->context, path);
}
bool HalStorage::rename(const char *from, const char *to) {
  StorageLock lock; if (!lock || !initialized || !mediaReady()) return false;
  generations.mutationAttempt(); return extended->rename(volume->context, from, to);
}
bool HalStorage::openFileForRead(const char*, const char *path, HalFile& file) {
  if (file.impl && !file.close()) return false;
  file = open(path); return file.isOpen() && !file.isDirectory();
}
bool HalStorage::openFileForRead(const char *mod, const std::string& path, HalFile& file) { return openFileForRead(mod, path.c_str(), file); }
bool HalStorage::openFileForRead(const char *mod, const String& path, HalFile& file) { return openFileForRead(mod, path.c_str(), file); }
bool HalStorage::openFileForWrite(const char*, const char *path, HalFile& file) {
  if (!path || std::strlen(path) >= RISC_STORAGE_VOLUME_PATH_MAX || (file.impl && !file.close())) return false;
  const char *slash = std::strrchr(path, '/');
  if (slash && slash != path && !mkdir(std::string(path, slash - path).c_str(), true)) return false;
  file = open(path, O_RDWR | O_CREAT | O_TRUNC); return file.isOpen();
}
bool HalStorage::openFileForWrite(const char *mod, const std::string& path, HalFile& file) { return openFileForWrite(mod, path.c_str(), file); }
bool HalStorage::openFileForWrite(const char *mod, const String& path, HalFile& file) { return openFileForWrite(mod, path.c_str(), file); }
void HalFile::flush() {
  HalStorage::StorageLock lock;
  if (!lock || !impl || !impl->handle || impl->directory) return;
  if (!extended->file_sync(volume->context, impl->handle)) { impl->error = 1; generations.mutationAttempt(); }
}
size_t HalFile::getName(char *name, size_t length) {
  if (!impl || !name || !length) return 0;
  const char *base = std::strrchr(impl->path.c_str(), '/'); base = base ? base + 1 : impl->path.c_str();
  const size_t n = std::min(length - 1, std::strlen(base));
  std::memcpy(name, base, n); name[n] = 0; return n;
}
uint64_t HalFile::fileSize64() { HalStorage::StorageLock lock; uint64_t size = 0, offset = 0; return lock && impl && impl->info(size, offset) ? size : 0; }
size_t HalFile::size() { return static_cast<size_t>(std::min<uint64_t>(fileSize64(), SIZE_MAX)); }
size_t HalFile::fileSize() { return size(); }
size_t HalFile::position() const { HalStorage::StorageLock lock; uint64_t size = 0, offset = 0; return lock && impl && impl->info(size, offset) ? static_cast<size_t>(offset) : 0; }
int HalFile::available() const {
  HalStorage::StorageLock lock; uint64_t size = 0, offset = 0;
  return lock && impl && impl->info(size, offset) && size > offset ? static_cast<int>(std::min<uint64_t>(INT_MAX, size - offset)) : 0;
}
bool HalFile::seek64(uint64_t offset) {
  HalStorage::StorageLock lock;
  return lock && impl && impl->handle && !impl->directory && extended->file_seek(volume->context, impl->handle, offset);
}
bool HalFile::seek(size_t offset) { return seek64(offset); }
bool HalFile::seekSet(size_t offset) { return seek64(offset); }
bool HalFile::seekCur(int64_t delta) {
  const uint64_t offset = position();
  if (delta < 0 && static_cast<uint64_t>(-(delta + 1)) + 1 > offset) return false;
  if (delta > 0 && static_cast<uint64_t>(delta) > UINT64_MAX - offset) return false;
  return seek64(delta < 0 ? offset - (static_cast<uint64_t>(-(delta + 1)) + 1) : offset + static_cast<uint64_t>(delta));
}
int HalFile::read(void *buffer, size_t count) {
  HalStorage::StorageLock lock;
  if (!lock || !impl || !impl->handle || impl->directory || !mediaReady() || (!buffer && count)) return -1;
  if (count > kMaxOperationBytes) { impl->error = 1; return -1; }
  size_t total = 0; const uint32_t began = millis();
  while (total < count) {
    if (millis() - began >= kOperationMs) { impl->error = 1; break; }
    const size_t got = volume->file_read(volume->context, impl->handle, static_cast<uint8_t*>(buffer) + total,
                                       std::min<size_t>(RISC_STORAGE_VOLUME_IO_MAX, count - total));
    if (extended->handle_error(volume->context, impl->handle, false)) { impl->error = 1; break; }
    if (!got) break;
    total += got; delay(1);
  }
  return !total && impl->error ? -1 : static_cast<int>(total);
}
int HalFile::read() { uint8_t byte; return read(&byte, 1) == 1 ? byte : -1; }
size_t HalFile::write(const void *buffer, size_t count) {
  HalStorage::StorageLock lock;
  if (!lock || !impl || !impl->handle || !impl->writer || !mediaReady() || (!buffer && count)) return 0;
  generations.mutationAttempt();
  if (count > kMaxOperationBytes) { impl->error = 1; return 0; }
  size_t total = 0; const uint32_t began = millis();
  while (total < count) {
    if (millis() - began >= kOperationMs) { impl->error = 1; break; }
    const size_t want = std::min<size_t>(RISC_STORAGE_VOLUME_IO_MAX, count - total);
    const size_t got = volume->file_write(volume->context, impl->handle, static_cast<const uint8_t*>(buffer) + total, want);
    total += got;
    if (impl->syncWrites && !extended->file_sync(volume->context, impl->handle)) { impl->error = 1; break; }
    if (got != want || extended->handle_error(volume->context, impl->handle, false)) { impl->error = 1; break; }
    delay(1);
  }
  return total;
}
size_t HalFile::write(uint8_t byte) { return write(&byte, 1); }
bool HalFile::isDirectory() const { return impl && impl->handle && impl->directory; }
void HalFile::rewindDirectory() {
  HalStorage::StorageLock lock;
  if (!lock || !impl || !impl->directory || !impl->handle) return;
  if (!extended->dir_rewind(volume->context, impl->handle)) impl->error = 1;
}
bool HalFile::close() { HalStorage::StorageLock lock; return lock && (!impl || impl->closeLocked()); }
HalFile HalFile::openNextFile() {
  HalStorage::StorageLock lock;
  if (!lock || !impl || !impl->handle || !impl->directory || impl->error || !mediaReady()) return {};
  risc_storage_dirent_v1 entry{};
  if (!volume->dir_next(volume->context, impl->handle, &entry)) {
    if (extended->handle_error(volume->context, impl->handle, true)) impl->error = 1;
    return {};
  }
  // Preserve Storage.open admission after an explicit unavailable transition,
  // even when the provider still reports ready. The lock is non-recursive.
  if (!lock.initialized()) { impl->error = 1; return {}; }
  // dir_next already supplies the type. Open the real child directly: a
  // redundant stat would walk the FAT parent again for every entry. Provider
  // opens still validate existence/type and own all handle/close semantics.
  const std::string path = impl->path + (impl->path == "/" ? "" : "/") + entry.name;
  const uint32_t handle = entry.is_directory ? volume->dir_open(volume->context, path.c_str())
      : extended->file_open(volume->context, path.c_str(), RISC_STORAGE_OPEN_READ);
  if (!handle) { impl->error = 1; return {}; }
  return HalFile(std::make_unique<Impl>(handle, entry.is_directory, false, path.c_str()));
}
uint8_t HalFile::getError() const {
  HalStorage::StorageLock lock;
  if (!lock || !impl) return 1;
  if (impl->error) return impl->error;
  return impl->handle && extended->handle_error(volume->context, impl->handle, impl->directory) ? 1 : 0;
}
bool HalFile::isOpen() const { HalStorage::StorageLock lock; return lock && impl && impl->handle && mediaReady(); }
HalFile::operator bool() const { return isOpen(); }
bool HalFile::rename(const char *path) {
  if (!impl || !path) return false;
  const std::string old = impl->path;
  if (!close() || !Storage.rename(old.c_str(), path)) return false;
  *this = Storage.open(path, impl->writer ? O_RDWR : O_RDONLY); return isOpen();
}
std::vector<String> HalStorage::listFiles(const char *path, int maximum) {
  std::vector<String> names; auto dir = open(path);
  const uint32_t began = millis();
  for (unsigned i = 0; i < 4096u && static_cast<int>(names.size()) < maximum && millis() - began < kOperationMs; ++i) {
    auto file = dir.openNextFile(); if (!file) break;
    if (!file.isDirectory()) { char name[RISC_STORAGE_VOLUME_NAME_MAX]; file.getName(name, sizeof(name)); names.emplace_back(name); }
    delay(1);
  }
  return names;
}
String HalStorage::readFile(const char *path) {
  auto file = open(path); String result; char buffer[256];
  const uint32_t began = millis();
  while (file && result.length() < 50000u && millis() - began < kOperationMs) {
    const int count = file.read(buffer, std::min<size_t>(sizeof(buffer), 50000u - result.length()));
    if (count <= 0) break;
    result.concat(buffer, count); delay(1);
  }
  return result;
}
size_t HalStorage::readFileToBuffer(const char *path, char *buffer, size_t size, size_t maximum) {
  if (!buffer || !size) return 0;
  buffer[0] = 0; auto file = open(path); if (!file) return 0;
  const size_t limit = std::min(size - 1, maximum ? maximum : size - 1);
  const int count = file.read(buffer, limit); const size_t got = count > 0 ? count : 0;
  buffer[got] = 0; return got;
}
bool HalStorage::readFileToStream(const char *path, Print& output, size_t chunk) {
  auto file = open(path); if (!file) return false;
  uint8_t buffer[256]; const uint32_t began = millis(); size_t total = 0;
  while (file.available()) {
    if (total >= kMaxOperationBytes || millis() - began >= kOperationMs) return false;
    const int count = file.read(buffer, chunk ? std::min(chunk, sizeof(buffer)) : sizeof(buffer));
    if (count <= 0 || output.write(buffer, count) != static_cast<size_t>(count)) return false;
    total += count; delay(1);
  }
  return !file.getError() && file.close();
}
bool HalStorage::writeFile(const char *path, const String& content) {
  HalFile file; if (!openFileForWrite("SD", path, file)) return false;
  const size_t count = file.write(content.c_str(), content.length()); file.flush();
  const bool ok = count == content.length() && !file.getError(); return file.close() && ok;
}
bool HalStorage::removeDir(const char *path) {
  // Explicit bounded stack, no recursive C++ call stack and no full rescans.
  struct Frame { std::string path; HalFile dir; };
  std::vector<Frame> stack;
  auto root = open(path); if (!root || !root.isDirectory()) return false;
  stack.push_back({path, std::move(root)});
  const uint32_t began = millis();
  for (unsigned work = 0; work < 4096u && !stack.empty(); ++work) {
    if (millis() - began >= kOperationMs) return false;
    auto& top = stack.back(); auto child = top.dir.openNextFile();
    if (!child) {
      if (top.dir.getError() || !top.dir.close() || !rmdir(top.path.c_str())) return false;
      stack.pop_back();
    } else {
      char name[RISC_STORAGE_VOLUME_NAME_MAX]; child.getName(name, sizeof(name));
      std::string childPath = top.path + "/" + name;
      if (child.isDirectory()) {
        if (stack.size() >= 6u) return false;
        stack.push_back({std::move(childPath), std::move(child)});
      } else if (!child.close() || !remove(childPath.c_str())) return false;
    }
    delay(1);
  }
  return stack.empty();
}
#endif
