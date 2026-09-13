#include "NativeAppLauncher.h"
#include <HalStorage.h>
#include <esp_vfs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <sys/stat.h>

namespace {
constexpr int kSlots = 4;
HalFile files[kSlots];
StaticSemaphore_t mutexStorage;
SemaphoreHandle_t mutex = nullptr;

struct Lock {
  Lock() { xSemaphoreTake(mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(mutex); }
};
bool validFd(int fd) { return fd >= 0 && fd < kSlots && files[fd].isOpen(); }

int openFile(const char* path, int flags, int) {
  Lock lock;
  if ((flags & O_ACCMODE) != O_RDONLY || (flags & (O_CREAT | O_TRUNC | O_APPEND))) {
    errno = EROFS;
    return -1;
  }
  for (int i = 0; i < kSlots; ++i) {
    if (files[i].isOpen()) continue;
    files[i] = Storage.open(path, O_RDONLY);
    if (!files[i] || files[i].isDirectory()) {
      files[i].close();
      errno = ENOENT;
      return -1;
    }
    if (files[i].fileSize64() < 52 || files[i].fileSize64() > 8u * 1024u * 1024u) {
      files[i].close();
      errno = EFBIG;
      return -1;
    }
    return i;
  }
  errno = EMFILE;
  return -1;
}
ssize_t readFile(int fd, void* dst, size_t size) {
  Lock lock;
  if (!validFd(fd)) { errno = EBADF; return -1; }
  const int result = files[fd].read(dst, size);
  if (result < 0) errno = EIO;
  return result;
}
off_t seekFile(int fd, off_t offset, int whence) {
  Lock lock;
  if (!validFd(fd)) { errno = EBADF; return -1; }
  int64_t base = 0;
  if (whence == SEEK_CUR) base = files[fd].position();
  else if (whence == SEEK_END) base = files[fd].fileSize64();
  else if (whence != SEEK_SET) { errno = EINVAL; return -1; }
  // Files served to this loader are bounded below INT32_MAX.
  const int64_t target = base + static_cast<int64_t>(offset);
  if (target < 0 || target > INT32_MAX) { errno = EINVAL; return -1; }
  if (!files[fd].seek64(target)) { errno = EIO; return -1; }
  return static_cast<off_t>(target);
}
int closeFile(int fd) {
  Lock lock;
  if (!validFd(fd)) { errno = EBADF; return -1; }
  const bool ok = files[fd].close();
  if (!ok) errno = EIO;
  return ok ? 0 : -1;
}
int statFile(int fd, struct stat* st) {
  Lock lock;
  if (!validFd(fd)) { errno = EBADF; return -1; }
  *st = {};
  st->st_mode = S_IFREG | S_IRUSR;
  st->st_size = files[fd].fileSize64();
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
