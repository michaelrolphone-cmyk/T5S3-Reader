#include "NativeAppLauncher.h"
#include "NativeAppVolumeVfs.h"
#include <HalStorage.h>
#include <esp_vfs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <algorithm>
#include <atomic>
#include <fcntl.h>
#include <sys/stat.h>

namespace {
constexpr int kSlots = 4;
HalFile files[kSlots];
// isOpen() also observes media readiness and a fallible inner lock. It cannot
// establish that an unavailable legacy handle has released its VFS slot.
bool legacyOwned[kSlots]{};
bool legacyAdmitted[kSlots]{};
// One explicitly supplied binding. It is immutable while registered, including
// when failed cleanup has retained a handle that was never returned to libc.
const risc_storage_volume_api_v1_ext* selected = nullptr;
esp_vfs_id_t selectedId = -1;
risc_storage_file_t selectedFiles[kSlots]{};
bool selectedAdmitted[kSlots]{};
// Nonpermanent IDF descriptors lose their global mapping after every close
// callback, including mutex timeout. Mark only selected callbacks lock-free.
std::atomic<bool> selectedClosePending[kSlots]{};
StaticSemaphore_t mutexStorage;
SemaphoreHandle_t mutex = nullptr;

// Bound descriptor-table contention separately from the underlying HalStorage
// and media calls. A timeout must not touch or discard a live slot, including
// close: the loader then refuses admission rather than pretending it closed.
constexpr uint32_t kDescriptorWaitMs = 1000;
struct Lock {
  bool held = false;
  Lock() : held(mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(kDescriptorWaitMs)) == pdTRUE) {}
  ~Lock() { if (held) xSemaphoreGive(mutex); }
  explicit operator bool() const { return held; }
};
bool occupied(int fd) { return selectedFiles[fd] || legacyOwned[fd]; }
bool validFd(int fd) {
  return fd >= 0 && fd < kSlots &&
         (selectedFiles[fd] ? selectedAdmitted[fd] && !selectedClosePending[fd].load()
                            : legacyAdmitted[fd] && files[fd].isOpen());
}
bool readOnly(int flags) {
  return (flags & O_ACCMODE) == O_RDONLY && !(flags & (O_CREAT | O_TRUNC | O_APPEND));
}
bool selectedInfo(int fd, uint64_t& size, uint64_t& offset) {
  return selected->base.ready(selected->base.context) &&
         selected->file_info(selected->base.context, selectedFiles[fd], &size, &offset) &&
         !selected->handle_error(selected->base.context, selectedFiles[fd], false) &&
         size >= 52 && size <= 8u * 1024u * 1024u && offset <= INT32_MAX;
}
bool closeLegacy(int fd) {
  if (!files[fd].close()) return false;
  legacyOwned[fd] = false;
  legacyAdmitted[fd] = false;
  return true;
}
bool closeSelected(int fd) {
  if (!selected->base.file_close(selected->base.context, selectedFiles[fd], true)) return false;
  selectedFiles[fd] = 0;
  selectedAdmitted[fd] = false;
  selectedClosePending[fd].store(false);
  return true;
}
int openSelectedLocked(const char* path, int flags) {
  if (!readOnly(flags)) { errno = EROFS; return -1; }
  if (!path || path[0] != '/' || !selected || !selected->base.ready(selected->base.context)) {
    errno = EIO; return -1;
  }
  for (int i = 0; i < kSlots; ++i) {
    if (occupied(i)) continue;
    selectedClosePending[i].store(false);
    // The existing extension admits ordinary files only, never directories.
    selectedFiles[i] = selected->file_open(selected->base.context, path, RISC_STORAGE_OPEN_READ);
    if (!selectedFiles[i]) { errno = ENOENT; return -1; }
    uint64_t size = 0, offset = 0;
    const bool info = selected->file_info(selected->base.context, selectedFiles[i], &size, &offset) &&
                      !selected->handle_error(selected->base.context, selectedFiles[i], false);
    const bool bounded = size >= 52 && size <= 8u * 1024u * 1024u;
    if (!info || !bounded || offset != 0) {
      // A false checked close retains this slot AND the registered provider.
      const bool closed = closeSelected(i);
      errno = !closed || !info || offset != 0 ? EIO : EFBIG;
      return -1;
    }
    selectedAdmitted[i] = true;
    return i;
  }
  errno = EMFILE;
  return -1;
}

int openFile(const char* path, int flags, int) {
  Lock lock;
  if (!lock) { errno = ETIMEDOUT; return -1; }
  if (!readOnly(flags)) {
    errno = EROFS;
    return -1;
  }
  for (int i = 0; i < kSlots; ++i) {
    if (occupied(i)) continue;
    files[i] = Storage.open(path, O_RDONLY);
    legacyOwned[i] = true;
    if (!files[i] || files[i].isDirectory()) {
      (void)closeLegacy(i);
      errno = ENOENT;
      return -1;
    }
    if (files[i].fileSize64() < 52 || files[i].fileSize64() > 8u * 1024u * 1024u) {
      (void)closeLegacy(i);
      errno = EFBIG;
      return -1;
    }
    legacyAdmitted[i] = true;
    return i;
  }
  errno = EMFILE;
  return -1;
}
ssize_t readFile(int fd, void* dst, size_t size) {
  Lock lock;
  if (!lock) { errno = ETIMEDOUT; return -1; }
  if (!validFd(fd)) { errno = EBADF; return -1; }
  if (selectedFiles[fd]) {
    if (!selected->base.ready(selected->base.context)) { errno = EIO; return -1; }
    if (!dst && size) { errno = EINVAL; return -1; }
    if (!size) return 0;
    // The ELF reader already requests <=4096 bytes and yields between calls.
    const size_t capacity = std::min<size_t>(size, RISC_STORAGE_VOLUME_IO_MAX);
    const size_t result = selected->base.file_read(selected->base.context, selectedFiles[fd], dst, capacity);
    if (result > capacity || selected->handle_error(selected->base.context, selectedFiles[fd], false)) {
      errno = EIO; return -1;
    }
    return static_cast<ssize_t>(result);
  }
  const int result = files[fd].read(dst, size);
  if (result < 0) errno = EIO;
  return result;
}
off_t seekFile(int fd, off_t offset, int whence) {
  Lock lock;
  if (!lock) { errno = ETIMEDOUT; return -1; }
  if (!validFd(fd)) { errno = EBADF; return -1; }
  int64_t base = 0;
  if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) { errno = EINVAL; return -1; }
  if (selectedFiles[fd]) {
    uint64_t size = 0, position = 0;
    if (!selectedInfo(fd, size, position)) { errno = EIO; return -1; }
    if (whence == SEEK_CUR) base = static_cast<int64_t>(position);
    else if (whence == SEEK_END) base = static_cast<int64_t>(size);
  } else {
    if (whence == SEEK_CUR) base = files[fd].position();
    else if (whence == SEEK_END) base = files[fd].fileSize64();
  }
  // Files served to this loader are bounded below INT32_MAX.
  if (base < 0 || base > INT32_MAX || offset < -base || offset > INT32_MAX - base) {
    errno = EINVAL; return -1;
  }
  const int64_t target = base + static_cast<int64_t>(offset);
  const bool okay = selectedFiles[fd]
      ? selected->file_seek(selected->base.context, selectedFiles[fd], target)
      : files[fd].seek64(target);
  if (!okay) { errno = EIO; return -1; }
  return static_cast<off_t>(target);
}
int closeFile(int fd) {
  Lock lock;
  if (!lock) { errno = ETIMEDOUT; return -1; }
  if (!validFd(fd)) { errno = EBADF; return -1; }
  const bool ok = selectedFiles[fd] ? closeSelected(fd) : closeLegacy(fd);
  if (!ok) errno = EIO;
  return ok ? 0 : -1;
}
int closeSelectedFile(int fd) {
  if (fd < 0 || fd >= kSlots) { errno = EBADF; return -1; }
  // IDF removes this nonpermanent global mapping after we return, even on
  // failure. The exclusive owner must wait for close to return before cleanup.
  selectedClosePending[fd].store(true);
  Lock lock;
  if (!lock) { errno = ETIMEDOUT; return -1; }
  if (!selectedFiles[fd] || !selectedAdmitted[fd]) { errno = EBADF; return -1; }
  if (!closeSelected(fd)) { errno = EIO; return -1; }
  return 0;
}
int statFile(int fd, struct stat* st) {
  Lock lock;
  if (!lock) { errno = ETIMEDOUT; return -1; }
  if (!validFd(fd)) { errno = EBADF; return -1; }
  uint64_t size = 0, position = 0;
  if (selectedFiles[fd]) {
    if (!selectedInfo(fd, size, position)) { errno = EIO; return -1; }
  } else size = files[fd].fileSize64();
  *st = {};
  st->st_mode = S_IFREG | S_IRUSR;
  st->st_size = size;
  return 0;
}
}  // namespace

// Called only inside launch_elf_app's exclusive session guard.
esp_err_t native_app_register_sd_vfs() {
  static bool registered = false;
  if (registered) return Storage.ready() ? ESP_OK : ESP_ERR_INVALID_STATE;
  if (!Storage.ready()) return ESP_ERR_INVALID_STATE;
  if (!mutex) mutex = xSemaphoreCreateMutexStatic(&mutexStorage);
  if (!mutex) return ESP_ERR_NO_MEM;
  esp_vfs_t vfs = {};
  vfs.flags = ESP_VFS_FLAG_DEFAULT;
  vfs.open = openFile;
  vfs.read = readFile;
  vfs.lseek = seekFile;
  vfs.close = closeFile;
  vfs.fstat = statFile;
  const esp_err_t err = esp_vfs_register("/sd", &vfs, nullptr);
  if (err == ESP_OK) registered = true;
  return err;
}

// Caller owns selection, authority and provider lifetime. Pathless registration
// gives an exact VFS identity: IDF permits duplicate path prefixes and name-based
// unregister could otherwise remove a foreign VFS. /sd is never rebound.
esp_err_t native_app_register_volume_vfs(const risc_storage_volume_api_v1* volume) {
  const auto* ext = risc_storage_volume_extension(volume);
  if (!ext || !volume->ready || !volume->file_read || !volume->file_close ||
      !ext->file_open || !ext->file_seek || !ext->file_info || !ext->handle_error) return ESP_ERR_INVALID_ARG;
  if (!mutex) mutex = xSemaphoreCreateMutexStatic(&mutexStorage);
  if (!mutex) return ESP_ERR_NO_MEM;
  Lock lock;
  if (!lock) return ESP_ERR_TIMEOUT;
  if (selected) {
    return selected == ext && volume->ready(volume->context) ? ESP_OK : ESP_ERR_INVALID_STATE;
  }
  if (!volume->ready(volume->context)) return ESP_ERR_INVALID_STATE;
  esp_vfs_t vfs = {};
  vfs.flags = ESP_VFS_FLAG_DEFAULT;
  vfs.read = readFile;
  vfs.lseek = seekFile;
  vfs.close = closeSelectedFile;
  vfs.fstat = statFile;
  const esp_err_t err = esp_vfs_register_with_id(&vfs, nullptr, &selectedId);
  if (err == ESP_OK) selected = ext;
  return err;
}

int native_app_open_volume_file(const risc_storage_volume_api_v1* volume, const char* path, int flags) {
  Lock lock;
  if (!lock) { errno = ETIMEDOUT; return -1; }
  if (!selected || &selected->base != volume) { errno = EINVAL; return -1; }
  const int local = openSelectedLocked(path, flags);
  if (local < 0) return -1;
  int fd = -1;
  const esp_err_t err = esp_vfs_register_fd_with_local_fd(selectedId, local, false, &fd);
  if (err == ESP_OK) return fd;
  // No libc descriptor escaped. Failed cleanup still owns the local token and
  // prevents unregistration, even though the caller has no descriptor to retry.
  selectedAdmitted[local] = false;
  const bool closed = closeSelected(local);
  errno = closed ? EMFILE : EIO;
  return -1;
}

esp_err_t native_app_unregister_volume_vfs(const risc_storage_volume_api_v1* volume) {
  Lock lock;
  if (!lock) return ESP_ERR_TIMEOUT;
  if (!selected || &selected->base != volume) return ESP_ERR_INVALID_STATE;
  // The owner has quiesced sessions and waited for every libc close to return.
  // Never close live admitted descriptors. Retry only unreturned tokens or
  // selected close attempts whose nonpermanent global mapping IDF discarded.
  for (int i = 0; i < kSlots; ++i)
    if (selectedFiles[i] && selectedAdmitted[i] && !selectedClosePending[i].load()) return ESP_ERR_INVALID_STATE;
  for (int i = 0; i < kSlots; ++i)
    if (selectedFiles[i] && !closeSelected(i)) return ESP_ERR_INVALID_STATE;
  const esp_err_t err = esp_vfs_unregister_with_id(selectedId);
  if (err == ESP_OK) { selected = nullptr; selectedId = -1; }
  return err;
}
