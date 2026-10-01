#define HAL_STORAGE_IMPL
#include "HalStorage.h"

#include <FS.h>  // need to be included before SdFat.h for compatibility with FS.h's File class
#include <Board.h>
#include <Logging.h>
#include <SdFat.h>

#include <cassert>
#include <cstring>

namespace {
constexpr uint32_t SD_SPI_FREQUENCY = 40000000;
SdFat sd;
StorageGenerationTracker storageGeneration;
bool writableFlags(oflag_t flags) { return (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND)) != 0; }
bool closeRaw(FsFile& file) {
  if (!file.isOpen()) return file.close();
  const bool error = file.getError() != 0;
  const bool closed = file.close();
  if (error || !closed) storageGeneration.mutationAttempt();
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

class HalStorage::StorageLock {
 public:
  StorageLock() { xSemaphoreTake(HalStorage::getInstance().storageMutex, portMAX_DELAY); }
  ~StorageLock() { xSemaphoreGive(HalStorage::getInstance().storageMutex); }
};

bool HalStorage::begin() {
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
}

bool HalStorage::ready() const { StorageLock lock; return initialized; }
StorageGenerationStamp HalStorage::generation() const { StorageLock lock; return storageGeneration.stamp(initialized); }
bool HalStorage::unchanged(const StorageGenerationStamp& stamp) const { return stamp.matches(generation()); }
void HalStorage::externalStorageBegin() { StorageLock lock; storageGeneration.externalBegin(); }
void HalStorage::externalStorageEnd(bool closed) { StorageLock lock; storageGeneration.externalEnd(closed); }
void HalStorage::externalStorageUncertain() { StorageLock lock; storageGeneration.externalUncertain(); }
bool HalStorage::reconcileExternalStorage() {
  {
    StorageLock lock;
    if (!storageGeneration.needsReconcile()) return initialized;
  }
  // begin rechecks ownership while locked. It cannot reset an active handle,
  // a concurrent raw session or permanently uncertain teardown.
  return begin();
}
void HalStorage::markUnavailable() { StorageLock lock; initialized = false; storageGeneration.mutationAttempt(); }



std::vector<String> HalStorage::listFiles(const char* path, int maxFiles) {
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
    content += static_cast<char>(f.read());
    readSize++;
  }
  closeRaw(f);
  return content;
}

bool HalStorage::readFileToStream(const char* path, Print& out, size_t chunkSize) {
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
    const int r = f.read(buf, toRead);
    if (r > 0) {
      out.write(buf, static_cast<size_t>(r));
    } else {
      break;
    }
  }

  closeRaw(f);
  return true;
}

size_t HalStorage::readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes) {
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
}

bool HalStorage::writeFile(const char* path, const String& content) {
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

  // SdFat is built with DESTRUCTOR_CLOSES_FILE, so destroying an open FsFile
  // can sync the card and end an Arduino SPI transaction. Keep the entire raw
  // FsFile destruction inside the same storage mutex used by every HalFile I/O
  // call; otherwise another task can acquire the SD bus between the final read
  // and this destructor and trigger SPI.endTransaction() from the wrong owner.
  HalStorage::StorageLock lock;
  impl->destroyLocked();
  impl.reset();
}

HalFile::HalFile(HalFile&&) = default;

HalFile& HalFile::operator=(HalFile&& other) {
  if (this == &other) {
    return *this;
  }

  // unique_ptr move-assignment destroys the previous Impl. Do that explicitly
  // under StorageLock for the same reason as the destructor before adopting the
  // incoming handle.
  if (impl) {
    HalStorage::StorageLock lock;
    impl->destroyLocked();
    impl.reset();
  }
  impl = std::move(other.impl);
  return *this;
}

HalFile HalStorage::open(const char* path, const oflag_t oflag) {
  StorageLock lock;
  const bool writable = writableFlags(oflag);
  if (writable) storageGeneration.mutationAttempt();
  auto opened = sd.open(path, oflag);
  if (!opened.isOpen() && (opened.getError() || sd.sdErrorCode())) storageGeneration.mutationAttempt();
  return HalFile(std::make_unique<HalFile::Impl>(std::move(opened), writable));
}

bool HalStorage::mkdir(const char* path, const bool pFlag) {
  StorageLock lock;
  storageGeneration.mutationAttempt();
  return sd.mkdir(path, pFlag);
}

bool HalStorage::exists(const char* path) {
  StorageLock lock;
  const bool found = sd.exists(path);
  if (!found && sd.sdErrorCode()) storageGeneration.mutationAttempt();
  return found;
}

bool HalStorage::remove(const char* path) {
  StorageLock lock;
  storageGeneration.mutationAttempt();
  return sd.remove(path);
}
bool HalStorage::rename(const char* oldPath, const char* newPath) {
  StorageLock lock;
  storageGeneration.mutationAttempt();
  return sd.rename(oldPath, newPath);
}

bool HalStorage::rmdir(const char* path) {
  StorageLock lock;
  storageGeneration.mutationAttempt();
  return sd.rmdir(path);
}

bool HalStorage::openFileForRead(const char* moduleName, const char* path, HalFile& file) {
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
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForRead(const char* moduleName, const String& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const char* path, HalFile& file) {
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
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const String& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::removeDir(const char* path) {
  StorageLock lock;
  storageGeneration.mutationAttempt();
  return removeDirUnlocked(path);
}

#define HAL_FILE_WRAPPED_CALL(method, ...) \
  HalStorage::StorageLock lock;            \
  assert(impl != nullptr);                 \
  const auto result = impl->file.method(__VA_ARGS__); \
  if (impl->file.getError()) storageGeneration.mutationAttempt(); \
  return result;

#define HAL_FILE_FORWARD_CALL(method, ...) \
  assert(impl != nullptr);                 \
  return impl->file.method(__VA_ARGS__);

void HalFile::flush() {
  HalStorage::StorageLock lock; assert(impl != nullptr);
  storageGeneration.mutationAttempt(); impl->file.flush();
  if (impl->file.getError()) storageGeneration.mutationAttempt();
}
size_t HalFile::getName(char* name, size_t len) { HAL_FILE_WRAPPED_CALL(getName, name, len); }
size_t HalFile::size() { HAL_FILE_WRAPPED_CALL(size, ); }
size_t HalFile::fileSize() { HAL_FILE_WRAPPED_CALL(fileSize, ); }
uint64_t HalFile::fileSize64() { HAL_FILE_WRAPPED_CALL(fileSize, ); }
bool HalFile::seek(size_t pos) { HAL_FILE_WRAPPED_CALL(seekSet, pos); }
bool HalFile::seek64(uint64_t pos) { HAL_FILE_WRAPPED_CALL(seekSet, pos); }
bool HalFile::seekCur(int64_t offset) { HAL_FILE_WRAPPED_CALL(seekCur, offset); }
bool HalFile::seekSet(size_t offset) { HAL_FILE_WRAPPED_CALL(seekSet, offset); }
int HalFile::available() const { HAL_FILE_WRAPPED_CALL(available, ); }
size_t HalFile::position() const { HAL_FILE_WRAPPED_CALL(position, ); }
int HalFile::read(void* buf, size_t count) {
  HalStorage::StorageLock lock; assert(impl != nullptr);
  const int read = impl->file.read(buf, count);
  if (read < 0 || impl->file.getError()) storageGeneration.mutationAttempt();
  return read;
}
int HalFile::read() {
  HalStorage::StorageLock lock; assert(impl != nullptr);
  const int read = impl->file.read();
  if (impl->file.getError()) storageGeneration.mutationAttempt();
  return read;
}
size_t HalFile::write(const void* buf, size_t count) {
  HalStorage::StorageLock lock; assert(impl != nullptr);
  storageGeneration.mutationAttempt(); return impl->file.write(buf, count);
}
size_t HalFile::write(uint8_t b) { return write(&b, 1); }
bool HalFile::rename(const char* newPath) {
  HalStorage::StorageLock lock; assert(impl != nullptr);
  storageGeneration.mutationAttempt(); return impl->file.rename(newPath);
}
bool HalFile::isDirectory() const { HAL_FILE_WRAPPED_CALL(isDirectory, ); }
void HalFile::rewindDirectory() {
  HalStorage::StorageLock lock; assert(impl != nullptr);
  impl->file.rewindDirectory();
  if (impl->file.getError()) storageGeneration.mutationAttempt();
}
bool HalFile::close() {
  HalStorage::StorageLock lock; assert(impl != nullptr);
  if (impl->writable && impl->tracked) storageGeneration.mutationAttempt();
  const bool closed = closeRaw(impl->file);
  if (impl->tracked && !impl->file.isOpen()) {
    storageGeneration.closed(impl->writable); impl->tracked = false;
  }
  return closed;
}
HalFile HalFile::openNextFile() {
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  auto child = impl->file.openNextFile();
  if (!child.isOpen() && impl->file.getError()) storageGeneration.mutationAttempt();
  return HalFile(std::make_unique<Impl>(std::move(child)));
}
uint8_t HalFile::getError() const { HAL_FILE_WRAPPED_CALL(getError, ); }
bool HalFile::isOpen() const { HalStorage::StorageLock lock; return impl != nullptr && impl->file.isOpen(); }
HalFile::operator bool() const { return isOpen(); }
