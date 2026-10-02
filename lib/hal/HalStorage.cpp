#define HAL_STORAGE_IMPL
#include "HalStorage.h"

#include <Board.h>
#if defined(BOARD_XTEINK_X4_PRO)
#include <Arduino.h>
#endif
#include <FS.h>  // need to be included before SdFat.h for compatibility with FS.h's File class
#include <Logging.h>
#include <SdFat.h>

#include <cassert>
#include <climits>
#include <cstring>
#include <limits>

#include "HalStorageLifecycle.h"
#include "SdSpiFault.h"

namespace {
#if defined(BOARD_XTEINK_X4_PRO)
const risc_storage_volume_api_v1* x4Volume = nullptr;
bool x4FileOwned = false;
bool x4VolumeReady() { return x4Volume && x4Volume->ready(x4Volume->context); }
#endif
// The X4 slot belongs to the native SD provider. Until HalStorage has a
// storage.volume adapter, every legacy SdFat entry point must fail closed.
bool storageBackendUnavailable() {
#if defined(BOARD_XTEINK_X4_PRO)
  return true;
#else
  return risc_sd_spi_faulted();
#endif
}
constexpr uint32_t SD_SPI_FREQUENCY = 40000000;
SdFat sd;
StorageGenerationTracker storageGeneration;
bool writableFlags(oflag_t flags) { return (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND)) != 0; }
bool closeRaw(FsFile& file, bool retainedOnFailure = false) {
  if (!file.isOpen()) return file.close();
  const bool error = file.getError() != 0;
  const bool closed = file.close();
  if (error || !closed) storageGeneration.mutationAttempt();
  // A failed close whose owner is being discarded has no safe retry boundary.
  // Never certify fresh metadata or reset potentially unsynced caches afterward.
  if (!closed && (!retainedOnFailure || !file.isOpen())) storageGeneration.externalUncertain();
  return closed;
}

void logSdError(const char* moduleName, const char* action, const char* path) {
  LOG_ERR(moduleName, "%s: %s (sdErr=0x%X data=0x%X)", action, path, sd.sdErrorCode(), sd.sdErrorData());
}

bool ensureParentDirUnlocked(const char* path) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  const char* slash = strrchr(path, '/');
  if (slash == nullptr || slash == path) {
    return true;
  }
  char parent[160];
  const size_t len = static_cast<size_t>(slash - path);
  if (len == 0 || len >= sizeof(parent)) {
    return false;
  }
  memcpy(parent, path, len);
  parent[len] = '\0';

  if (sd.exists(parent)) {
    FsFile existing = sd.open(parent);
    const bool isDir = existing && existing.isDirectory();
    closeRaw(existing);
    if (isDir) {
      return true;
    }
    LOG_ERR("SD", "Parent path is not a directory, removing: %s", parent);
    sd.remove(parent);
  }
  if (sd.mkdir(parent, true)) {
    LOG_DBG("SD", "Created directory: %s", parent);
    return true;
  }
  logSdError("SD", "Failed to create parent directory", parent);
  return false;
}

bool openFileForReadUnlocked(const char* moduleName, const char* path, FsFile& file) {
  if (!sd.exists(path)) {
    LOG_ERR(moduleName, "File does not exist: %s", path);
    return false;
  }

  file = sd.open(path, O_RDONLY);
  if (!file) {
    logSdError(moduleName, "Failed to open file for reading", path);
    return false;
  }
  return true;
}

bool openFileForWriteUnlocked(const char* moduleName, const char* path, FsFile& file) {
  storageGeneration.mutationAttempt();
  ensureParentDirUnlocked(path);
  file = sd.open(path, O_RDWR | O_CREAT | O_TRUNC);
  if (!file) {
    logSdError(moduleName, "Failed to open file for writing", path);
    ensureParentDirUnlocked(path);
    file = sd.open(path, O_WRONLY | O_CREAT | O_TRUNC);
  }
  if (!file) {
    logSdError(moduleName, "Retry open for writing failed", path);
    return false;
  }
  return true;
}

bool removeDirUnlocked(const char* path) {
  auto dir = sd.open(path);
  if (!dir) {
    return false;
  }
  if (!dir.isDirectory()) {
    closeRaw(dir);
    return false;
  }

  auto file = dir.openNextFile();
  char name[128];
  while (file) {
    risc_sd_spi_guard();
    String filePath = path;
    if (!filePath.endsWith("/")) {
      filePath += "/";
    }
    file.getName(name, sizeof(name));
    filePath += name;

    const bool ok = file.isDirectory() ? removeDirUnlocked(filePath.c_str()) : sd.remove(filePath.c_str());
    closeRaw(file);
    if (!ok) {
      closeRaw(dir);
      return false;
    }
    file = dir.openNextFile();
  }

  closeRaw(dir);
  return sd.rmdir(path);
}
}  // namespace

HalStorage HalStorage::instance;

HalStorage::HalStorage() {
  storageMutex = xSemaphoreCreateMutex();
  assert(storageMutex != nullptr);
}

#if defined(BOARD_XTEINK_X4_PRO)
bool HalStorage::bindVolume(const risc_storage_volume_api_v1* volume) {
  if (x4FileOwned || !volume || volume->api_version != RISC_STORAGE_VOLUME_API_V1 ||
      volume->struct_size < sizeof(*volume) || !volume->ready || !volume->stat ||
      !volume->dir_open || !volume->dir_next || !volume->dir_close ||
      !volume->file_open_read || !volume->file_read || !volume->file_close) return false;
  x4Volume = volume;
  return x4VolumeReady();
}
#endif

class HalStorage::StorageLock {
 public:
  StorageLock() {
    risc_sd_spi_begin_operation();
#if defined(RISCRTE_SD_SPI_FAULT_PORT) || defined(RISCRTE_SD_SPI_FAULT_TEST)
    risc_sd_spi_wait_lock(HalStorage::getInstance().storageMutex);
#else
    xSemaphoreTake(HalStorage::getInstance().storageMutex, portMAX_DELAY);
#endif
  }
  ~StorageLock() {
    risc_sd_spi_end_operation();  // Failed owners cannot unlock/unwind here.
    xSemaphoreGive(HalStorage::getInstance().storageMutex);
  }
};

bool HalStorage::begin() {
#if defined(BOARD_XTEINK_X4_PRO)
  // X4's slot is native one-bit SDMMC. The legacy SdFat SPI transport must
  // never probe that bus or become an implicit fallback for storage.volume.
  return x4VolumeReady();
#else
  if (storageBackendUnavailable()) return false;
  StorageLock lock;
  if (!storageGeneration.mountAttempt()) {
    LOG_ERR("SD", "Remount refused while handles or uncontrolled access remain");
    return false;
  }
  Board::prepareSdBus();
  initialized = sd.begin(BoardPins::SdCs, SD_SPI_FREQUENCY);
  storageGeneration.mounted(initialized);
  if (initialized) {
    LOG_INF("SD", "SD card detected");
  } else {
    LOG_ERR("SD", "SD card not detected");
  }
  return initialized;
#endif
}

bool HalStorage::ready() const {
#if defined(BOARD_XTEINK_X4_PRO)
  return x4VolumeReady();
#endif
  if (storageBackendUnavailable()) return false;
  StorageLock lock;
  return initialized;
}
StorageGenerationStamp HalStorage::generation() const {
  if (storageBackendUnavailable()) return {};
  StorageLock lock;
  return storageGeneration.stamp(initialized);
}
bool HalStorage::unchanged(const StorageGenerationStamp& stamp) const { return stamp.matches(generation()); }
void HalStorage::invalidateObservations() {
  if (storageBackendUnavailable()) return;
  StorageLock lock;
  storageGeneration.mutationAttempt();
}
void HalStorage::externalStorageBegin() {
  if (storageBackendUnavailable()) return;
  StorageLock lock;
  storageGeneration.externalBegin();
}
void HalStorage::externalStorageEnd(bool closed) {
  if (storageBackendUnavailable()) return;
  StorageLock lock;
  storageGeneration.externalEnd(closed);
}
void HalStorage::externalStorageUncertain() {
  if (storageBackendUnavailable()) return;
  StorageLock lock;
  storageGeneration.externalUncertain();
}
bool HalStorage::reconcileExternalStorage() {
  if (storageBackendUnavailable()) return false;
  {
    StorageLock lock;
    if (!storageGeneration.needsReconcile()) return initialized;
  }
  // begin rechecks ownership while locked. It cannot reset an active handle,
  // a concurrent raw session or permanently uncertain teardown.
  return begin();
}
void HalStorage::markUnavailable() {
  if (storageBackendUnavailable()) return;
  StorageLock lock;
  initialized = false;
  storageGeneration.mutationAttempt();
}
void halStorageMediaUnavailable() { Storage.markUnavailable(); }

std::vector<String> HalStorage::listFiles(const char* path, int maxFiles) {
#if defined(BOARD_XTEINK_X4_PRO)
  std::vector<String> names;
  if (!x4VolumeReady() || !path || maxFiles <= 0) return names;
  const risc_storage_dir_t dir = x4Volume->dir_open(x4Volume->context, path);
  if (dir == RISC_STORAGE_DIR_INVALID) return names;
  risc_storage_dirent_v1 item{};
  for (int i = 0; i < maxFiles && i < 128 && x4VolumeReady() &&
                  x4Volume->dir_next(x4Volume->context, dir, &item); ++i) {
    if (!item.is_directory) names.emplace_back(item.name);
  }
  x4Volume->dir_close(x4Volume->context, dir);
  return names;
#endif
  if (storageBackendUnavailable()) return {};
  StorageLock lock;
  std::vector<String> ret;
  if (!initialized) {
    LOG_ERR("SD", "Not initialized, returning empty file list");
    return ret;
  }

  auto root = sd.open(path);
  if (!root) {
    LOG_ERR("SD", "Failed to open directory: %s", path);
    return ret;
  }
  if (!root.isDirectory()) {
    LOG_ERR("SD", "Path is not a directory: %s", path);
    closeRaw(root);
    return ret;
  }

  int count = 0;
  char name[128];
  while (count < maxFiles) {
    risc_sd_spi_guard();
    auto f = root.openNextFile();
    if (!f) {
      break;
    }
    if (f.isDirectory()) {
      closeRaw(f);
      continue;
    }
    f.getName(name, sizeof(name));
    ret.emplace_back(name);
    closeRaw(f);
    count++;
  }
  closeRaw(root);
  return ret;
}

String HalStorage::readFile(const char* path) {
#if defined(BOARD_XTEINK_X4_PRO)
  HalFile file = open(path, O_RDONLY);
  if (!file) return "";
  String content;
  const size_t limit = file.size() < 50000u ? file.size() : 50000u;
  for (size_t i = 0; i < limit; ++i) {
    const int c = file.read();
    if (c < 0) break;
    content += static_cast<char>(c);
    if ((i & 255u) == 255u) delay(1);
  }
  return content;
#else
  if (storageBackendUnavailable()) return "";
  StorageLock lock;
  if (!initialized) {
    LOG_ERR("SD", "Not initialized; cannot read file: %s", path);
    return "";
  }

  FsFile f;
  if (!openFileForReadUnlocked("SD", path, f)) {
    return "";
  }

  String content;
  constexpr size_t maxSize = 50000;
  size_t readSize = 0;
  while (f.available() && readSize < maxSize) {
    risc_sd_spi_guard();
    content += static_cast<char>(f.read());
    readSize++;
  }
  closeRaw(f);
  return content;
#endif
}

bool HalStorage::readFileToStream(const char* path, Print& out, size_t chunkSize) {
#if defined(BOARD_XTEINK_X4_PRO)
  HalFile file = open(path, O_RDONLY);
  if (!file) return false;
  uint8_t buffer[256];
  const size_t chunk = chunkSize && chunkSize < sizeof(buffer) ? chunkSize : sizeof(buffer);
  while (file.available() > 0) {
    const int count = file.read(buffer, chunk);
    if (count <= 0 || out.write(buffer, static_cast<size_t>(count)) != static_cast<size_t>(count)) return false;
    delay(1);
  }
  return x4VolumeReady();
#else
  if (storageBackendUnavailable()) return false;
  StorageLock lock;
  if (!initialized) {
    LOG_ERR("SD", "Not initialized; cannot stream file: %s", path);
    return false;
  }

  FsFile f;
  if (!openFileForReadUnlocked("SD", path, f)) {
    return false;
  }

  constexpr size_t localBufSize = 256;
  uint8_t buf[localBufSize];
  const size_t toRead = (chunkSize == 0) ? localBufSize : (chunkSize < localBufSize ? chunkSize : localBufSize);

  while (f.available()) {
    risc_sd_spi_guard();
    const int r = f.read(buf, toRead);
    if (r > 0) {
      out.write(buf, static_cast<size_t>(r));
    } else {
      break;
    }
  }

  closeRaw(f);
  return true;
#endif
}

size_t HalStorage::readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes) {
#if defined(BOARD_XTEINK_X4_PRO)
  if (!buffer || !bufferSize) return 0;
  buffer[0] = 0;
  HalFile file = open(path, O_RDONLY);
  if (!file) return 0;
  const size_t limit = maxBytes && maxBytes < bufferSize - 1u ? maxBytes : bufferSize - 1u;
  size_t total = 0;
  while (total < limit) {
    const size_t want = limit - total < 256u ? limit - total : 256u;
    const int count = file.read(buffer + total, want);
    if (count <= 0) break;
    total += static_cast<size_t>(count);
    delay(1);
  }
  buffer[total] = 0;
  return total;
#else
  if (storageBackendUnavailable()) return 0;
  StorageLock lock;
  if (!buffer || bufferSize == 0) {
    return 0;
  }
  if (!initialized) {
    LOG_ERR("SD", "Not initialized; cannot read file to buffer: %s", path);
    buffer[0] = '\0';
    return 0;
  }

  FsFile f;
  if (!openFileForReadUnlocked("SD", path, f)) {
    buffer[0] = '\0';
    return 0;
  }

  const size_t maxToRead = (maxBytes == 0) ? (bufferSize - 1) : min(maxBytes, bufferSize - 1);
  size_t total = 0;

  while (f.available() && total < maxToRead) {
    risc_sd_spi_guard();
    constexpr size_t chunk = 64;
    const size_t want = maxToRead - total;
    const size_t readLen = (want < chunk) ? want : chunk;
    const int r = f.read(buffer + total, readLen);
    if (r > 0) {
      total += static_cast<size_t>(r);
    } else {
      break;
    }
  }

  buffer[total] = '\0';
  closeRaw(f);
  return total;
#endif
}

bool HalStorage::writeFile(const char* path, const String& content) {
  if (storageBackendUnavailable()) return false;
  StorageLock lock;
  storageGeneration.mutationAttempt();
  if (!initialized) {
    LOG_ERR("SD", "Not initialized; cannot write file: %s", path);
    return false;
  }

  if (!ensureParentDirUnlocked(path)) {
    return false;
  }

  if (sd.exists(path)) {
    if (!sd.remove(path)) {
      logSdError("SD", "Failed to remove existing file before write", path);
    }
  }

  FsFile f;
  if (!openFileForWriteUnlocked("SD", path, f)) {
    return false;
  }

  const size_t written = f.print(content);
  const bool synced = f.sync();
  const bool closed = closeRaw(f);
  if (!synced || !closed || written != content.length()) {
    storageGeneration.mutationAttempt();
    LOG_ERR("SD", "Short write to %s (%u of %u)", path, static_cast<unsigned>(written),
            static_cast<unsigned>(content.length()));
    return false;
  }
  return true;
}

bool HalStorage::ensureDirectoryExists(const char* path) {
  if (storageBackendUnavailable()) return false;
  StorageLock lock;
  storageGeneration.mutationAttempt();
  if (!initialized) {
    LOG_ERR("SD", "Not initialized; cannot create directory: %s", path);
    return false;
  }

  if (sd.exists(path)) {
    FsFile dir = sd.open(path);
    const bool isDirectory = dir && dir.isDirectory();
    closeRaw(dir);
    if (isDirectory) {
      return true;
    }
    LOG_ERR("SD", "Path exists and is not a directory, removing: %s", path);
    sd.remove(path);
  }

  if (sd.mkdir(path, true)) {
    LOG_DBG("SD", "Created directory: %s", path);
    return true;
  }

  logSdError("SD", "Failed to create directory", path);
  return false;
}

class HalFile::Impl {
 public:
#if defined(BOARD_XTEINK_X4_PRO)
  Impl(const risc_storage_volume_api_v1* volume, risc_storage_file_t handle,
       uint64_t length, const char* path)
      : volume(volume), handle(handle), length(length), name(path ? path : "") {}
  ~Impl() {
    if (volume && handle != RISC_STORAGE_FILE_INVALID) {
      (void)volume->file_close(volume->context, handle, false);
      x4FileOwned = false;
    }
  }
  const risc_storage_volume_api_v1* volume = nullptr;
  risc_storage_file_t handle = RISC_STORAGE_FILE_INVALID;
  uint64_t length = 0;
  uint64_t offset = 0;
  std::string name;
#endif
  Impl(FsFile&& fsFile, bool writable = false) : file(std::move(fsFile)), writable(writable) {
    if (file.isOpen()) tracked = storageGeneration.opened(writable);
  }
  void destroyLocked() {
    if (tracked) {
      if (writable) storageGeneration.mutationAttempt();
      (void)closeRaw(file);
      if (file.isOpen()) storageGeneration.mutationAttempt();
      storageGeneration.closed(writable);
      tracked = false;
    }
  }
  FsFile file;
  bool writable = false;
  bool tracked = false;
};

HalFile::HalFile() = default;

HalFile::HalFile(std::unique_ptr<Impl> impl) : impl(std::move(impl)) {}

HalFile::~HalFile() {
  if (!impl) {
    return;
  }
#if defined(BOARD_XTEINK_X4_PRO)
  impl.reset();
  return;
#endif

  // SdFat is built with DESTRUCTOR_CLOSES_FILE, so destroying an open FsFile
  // can sync the card and end an Arduino SPI transaction. Keep the entire raw
  // FsFile destruction inside the same storage mutex used by every HalFile I/O
  // call; otherwise another task can acquire the SD bus between the final read
  // and this destructor and trigger SPI.endTransaction() from the wrong owner.
  if (storageBackendUnavailable()) {
    (void)impl.release();
    return;
  }
  HalStorage::StorageLock lock;
  impl->destroyLocked();
  impl.reset();
}

HalFile::HalFile(HalFile&&) = default;

HalFile& HalFile::operator=(HalFile&& other) {
  if (this == &other) {
    return *this;
  }
#if defined(BOARD_XTEINK_X4_PRO)
  impl.reset();
  impl = std::move(other.impl);
  return *this;
#endif

  // unique_ptr move-assignment destroys the previous Impl. Do that explicitly
  // under StorageLock for the same reason as the destructor before adopting the
  // incoming handle.
  if (impl) {
    if (storageBackendUnavailable()) {
      (void)impl.release();  // Raw FsFile destructor may still sync/unlock SPI.
      impl = std::move(other.impl);
      return *this;
    }
    HalStorage::StorageLock lock;
    impl->destroyLocked();
    impl.reset();
  }
  impl = std::move(other.impl);
  return *this;
}

HalFile HalStorage::open(const char* path, const oflag_t oflag) {
#if defined(BOARD_XTEINK_X4_PRO)
  if (!x4VolumeReady() || !path || writableFlags(oflag) || x4FileOwned) return {};
  uint64_t length = 0;
  const risc_storage_file_t handle = x4Volume->file_open_read(x4Volume->context, path, &length);
  if (handle == RISC_STORAGE_FILE_INVALID) return {};
  x4FileOwned = true;
  return HalFile(std::make_unique<HalFile::Impl>(x4Volume, handle, length, path));
#endif
  if (storageBackendUnavailable()) return {};
  StorageLock lock;
  const bool writable = writableFlags(oflag);
  if (writable) storageGeneration.mutationAttempt();
  auto opened = sd.open(path, oflag);
  if (!opened.isOpen() && (opened.getError() || sd.sdErrorCode())) storageGeneration.mutationAttempt();
  return HalFile(std::make_unique<HalFile::Impl>(std::move(opened), writable));
}

bool HalStorage::mkdir(const char* path, const bool pFlag) {
  if (storageBackendUnavailable()) return false;
  StorageLock lock;
  storageGeneration.mutationAttempt();
  return sd.mkdir(path, pFlag);
}

bool HalStorage::exists(const char* path) {
#if defined(BOARD_XTEINK_X4_PRO)
  uint64_t size = 0;
  bool directory = false;
  return x4VolumeReady() && path && x4Volume->stat(x4Volume->context, path, &size, &directory);
#endif
  if (storageBackendUnavailable()) return false;
  StorageLock lock;
  const bool found = sd.exists(path);
  if (!found && sd.sdErrorCode()) storageGeneration.mutationAttempt();
  return found;
}

bool HalStorage::remove(const char* path) {
  if (storageBackendUnavailable()) return false;
  StorageLock lock;
  storageGeneration.mutationAttempt();
  return sd.remove(path);
}
bool HalStorage::rename(const char* oldPath, const char* newPath) {
  if (storageBackendUnavailable()) return false;
  StorageLock lock;
  storageGeneration.mutationAttempt();
  return sd.rename(oldPath, newPath);
}

bool HalStorage::rmdir(const char* path) {
  if (storageBackendUnavailable()) return false;
  StorageLock lock;
  storageGeneration.mutationAttempt();
  return sd.rmdir(path);
}

bool HalStorage::openFileForRead(const char* moduleName, const char* path, HalFile& file) {
#if defined(BOARD_XTEINK_X4_PRO)
  (void)moduleName;
  if (x4FileOwned) return false;
  file = open(path, O_RDONLY);
  return static_cast<bool>(file);
#endif
  if (storageBackendUnavailable()) return false;
  std::unique_ptr<HalFile::Impl> opened;
  bool ok = false;
  {
    StorageLock lock;
    FsFile fsFile;
    ok = openFileForReadUnlocked(moduleName, path, fsFile);
    if (!ok && sd.sdErrorCode()) storageGeneration.mutationAttempt();
    opened = std::make_unique<HalFile::Impl>(std::move(fsFile), false);
  }
  // Assignment may close a previous destination. Never hold the non-recursive
  // storage lock while assigning; ownership/counting already moved into Impl.
  file = HalFile(std::move(opened));
  return ok;
}

bool HalStorage::openFileForRead(const char* moduleName, const std::string& path, HalFile& file) {
#if defined(BOARD_XTEINK_X4_PRO)
  return openFileForRead(moduleName, path.c_str(), file);
#endif
  if (storageBackendUnavailable()) return false;
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForRead(const char* moduleName, const String& path, HalFile& file) {
#if defined(BOARD_XTEINK_X4_PRO)
  return openFileForRead(moduleName, path.c_str(), file);
#endif
  if (storageBackendUnavailable()) return false;
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const char* path, HalFile& file) {
  if (storageBackendUnavailable()) return false;
  std::unique_ptr<HalFile::Impl> opened;
  bool ok = false;
  {
    StorageLock lock;
    FsFile fsFile;
    ok = openFileForWriteUnlocked(moduleName, path, fsFile);
    opened = std::make_unique<HalFile::Impl>(std::move(fsFile), true);
  }
  // Assignment may close a previous destination. Never hold the non-recursive
  // storage lock while assigning; ownership/counting already moved into Impl.
  file = HalFile(std::move(opened));
  return ok;
}

bool HalStorage::openFileForWrite(const char* moduleName, const std::string& path, HalFile& file) {
  if (storageBackendUnavailable()) return false;
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const String& path, HalFile& file) {
  if (storageBackendUnavailable()) return false;
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::removeDir(const char* path) {
  if (storageBackendUnavailable()) return false;
  StorageLock lock;
  storageGeneration.mutationAttempt();
  return removeDirUnlocked(path);
}

#define HAL_FILE_WRAPPED_CALL(method, ...)                        \
  if (storageBackendUnavailable()) return {};                           \
  HalStorage::StorageLock lock;                                   \
  assert(impl != nullptr);                                        \
  const auto result = impl->file.method(__VA_ARGS__);             \
  if (impl->file.getError()) storageGeneration.mutationAttempt(); \
  return result;

#define HAL_FILE_FORWARD_CALL(method, ...) \
  assert(impl != nullptr);                 \
  return impl->file.method(__VA_ARGS__);

void HalFile::flush() {
#if defined(BOARD_XTEINK_X4_PRO)
  return;
#endif
  if (storageBackendUnavailable()) return;
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  storageGeneration.mutationAttempt();
  impl->file.flush();
  if (impl->file.getError()) storageGeneration.mutationAttempt();
}
size_t HalFile::getName(char* name, size_t len) {
#if defined(BOARD_XTEINK_X4_PRO)
  if (!impl || !impl->volume || !name || !len) return 0;
  const char* base = std::strrchr(impl->name.c_str(), '/');
  base = base ? base + 1 : impl->name.c_str();
  const size_t length = std::strlen(base);
  const size_t copied = length < len - 1u ? length : len - 1u;
  std::memcpy(name, base, copied);
  name[copied] = 0;
  return copied;
#endif
  HAL_FILE_WRAPPED_CALL(getName, name, len);
}
size_t HalFile::size() {
#if defined(BOARD_XTEINK_X4_PRO)
  return impl && impl->volume ?
      static_cast<size_t>(impl->length > SIZE_MAX ? SIZE_MAX : impl->length) : 0;
#endif
  HAL_FILE_WRAPPED_CALL(size, );
}
size_t HalFile::fileSize() {
#if defined(BOARD_XTEINK_X4_PRO)
  return size();
#endif
  HAL_FILE_WRAPPED_CALL(fileSize, );
}
uint64_t HalFile::fileSize64() {
#if defined(BOARD_XTEINK_X4_PRO)
  return impl && impl->volume ? impl->length : 0;
#endif
  HAL_FILE_WRAPPED_CALL(fileSize, );
}
bool HalFile::seek(size_t pos) {
#if defined(BOARD_XTEINK_X4_PRO)
  return seek64(pos);
#endif
  HAL_FILE_WRAPPED_CALL(seekSet, pos);
}
bool HalFile::seek64(uint64_t pos) {
#if defined(BOARD_XTEINK_X4_PRO)
  if (!impl || !impl->volume || impl->handle == RISC_STORAGE_FILE_INVALID ||
      !x4VolumeReady() || pos > impl->length || pos > 131072u) return false;
  if (pos == impl->offset) return true;
  if (pos < impl->offset) {
    (void)impl->volume->file_close(impl->volume->context, impl->handle, false);
    impl->handle = impl->volume->file_open_read(impl->volume->context, impl->name.c_str(), &impl->length);
    impl->offset = 0;
    if (impl->handle == RISC_STORAGE_FILE_INVALID) { x4FileOwned = false; return false; }
  }
  uint8_t skip[256];
  const unsigned long began = millis();
  while (impl->offset < pos && millis() - began < 3000u && x4VolumeReady()) {
    const size_t want = pos - impl->offset < sizeof(skip) ?
        static_cast<size_t>(pos - impl->offset) : sizeof(skip);
    const size_t count = impl->volume->file_read(impl->volume->context, impl->handle, skip, want);
    if (!count) return false;
    impl->offset += count;
    delay(1);
  }
  return impl->offset == pos;
#endif
  HAL_FILE_WRAPPED_CALL(seekSet, pos);
}
bool HalFile::seekCur(int64_t offset) {
#if defined(BOARD_XTEINK_X4_PRO)
  if (!impl || !impl->volume || offset < -static_cast<int64_t>(impl->offset)) return false;
  return seek64(static_cast<uint64_t>(static_cast<int64_t>(impl->offset) + offset));
#endif
  HAL_FILE_WRAPPED_CALL(seekCur, offset);
}
bool HalFile::seekSet(size_t offset) {
#if defined(BOARD_XTEINK_X4_PRO)
  return seek64(offset);
#endif
  HAL_FILE_WRAPPED_CALL(seekSet, offset);
}
int HalFile::available() const {
#if defined(BOARD_XTEINK_X4_PRO)
  if (!impl || impl->handle == RISC_STORAGE_FILE_INVALID || !x4VolumeReady() ||
      impl->offset >= impl->length) return 0;
  const uint64_t remaining = impl->length - impl->offset;
  return remaining > static_cast<uint64_t>(INT_MAX) ? INT_MAX : static_cast<int>(remaining);
#endif
  HAL_FILE_WRAPPED_CALL(available, );
}
size_t HalFile::position() const {
#if defined(BOARD_XTEINK_X4_PRO)
  return impl && impl->volume ? static_cast<size_t>(impl->offset) : 0;
#endif
  HAL_FILE_WRAPPED_CALL(position, );
}
int HalFile::read(void* buf, size_t count) {
#if defined(BOARD_XTEINK_X4_PRO)
  if (!impl || impl->handle == RISC_STORAGE_FILE_INVALID || !x4VolumeReady() || !buf) return -1;
  if (!count || impl->offset >= impl->length) return 0;
  const size_t limit = count < 4096u ? count : 4096u;
  size_t total = 0;
  const unsigned long began = millis();
  while (total < limit && impl->offset < impl->length && millis() - began < 3000u) {
    const size_t want = limit - total < 512u ? limit - total : 512u;
    const size_t got = impl->volume->file_read(impl->volume->context, impl->handle,
                                               static_cast<uint8_t*>(buf) + total, want);
    if (!got) return total ? static_cast<int>(total) : -1;
    impl->offset += got;
    total += got;
    delay(1);
  }
  return total ? static_cast<int>(total) : -1;
#endif
  if (storageBackendUnavailable()) return -1;
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  const int read = impl->file.read(buf, count);
  if (read < 0 || impl->file.getError()) storageGeneration.mutationAttempt();
  return read;
}
int HalFile::read() {
#if defined(BOARD_XTEINK_X4_PRO)
  uint8_t value = 0;
  return read(&value, 1u) == 1 ? value : -1;
#endif
  if (storageBackendUnavailable()) return -1;
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  const int read = impl->file.read();
  if (impl->file.getError()) storageGeneration.mutationAttempt();
  return read;
}
size_t HalFile::write(const void* buf, size_t count) {
  if (storageBackendUnavailable()) return 0;
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  storageGeneration.mutationAttempt();
  return impl->file.write(buf, count);
}
size_t HalFile::write(uint8_t b) {
  if (storageBackendUnavailable()) return 0;
  return write(&b, 1);
}
bool HalFile::rename(const char* newPath) {
  if (storageBackendUnavailable()) return false;
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  storageGeneration.mutationAttempt();
  return impl->file.rename(newPath);
}
bool HalFile::isDirectory() const {
#if defined(BOARD_XTEINK_X4_PRO)
  return false;
#endif
  HAL_FILE_WRAPPED_CALL(isDirectory, );
}
void HalFile::rewindDirectory() {
  if (storageBackendUnavailable()) return;
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  impl->file.rewindDirectory();
  if (impl->file.getError()) storageGeneration.mutationAttempt();
}
bool HalFile::close() {
#if defined(BOARD_XTEINK_X4_PRO)
  if (!impl || !impl->volume || impl->handle == RISC_STORAGE_FILE_INVALID) return false;
  const bool closed = impl->volume->file_close(impl->volume->context, impl->handle, false);
  impl->handle = RISC_STORAGE_FILE_INVALID;
  x4FileOwned = false;
  return closed;
#else
  if (storageBackendUnavailable()) return false;
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  if (impl->writable && impl->tracked) storageGeneration.mutationAttempt();
  const bool closed = closeRaw(impl->file, true);
  if (impl->tracked && !impl->file.isOpen()) {
    storageGeneration.closed(impl->writable);
    impl->tracked = false;
  }
  return closed;
#endif
}
HalFile HalFile::openNextFile() {
  if (storageBackendUnavailable()) return {};
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  auto child = impl->file.openNextFile();
  if (!child.isOpen() && impl->file.getError()) storageGeneration.mutationAttempt();
  return HalFile(std::make_unique<Impl>(std::move(child)));
}
uint8_t HalFile::getError() const {
#if defined(BOARD_XTEINK_X4_PRO)
  return !impl || !impl->volume || !x4VolumeReady() ? 1u : 0u;
#endif
  if (storageBackendUnavailable()) return 255;
  HAL_FILE_WRAPPED_CALL(getError, );
}
bool HalFile::isOpen() const {
#if defined(BOARD_XTEINK_X4_PRO)
  return impl && impl->volume && impl->handle != RISC_STORAGE_FILE_INVALID && x4VolumeReady();
#endif
  if (storageBackendUnavailable()) return false;
  HalStorage::StorageLock lock;
  return impl != nullptr && impl->file.isOpen();
}
HalFile::operator bool() const { return isOpen(); }
