// Explicit CAM boot/module-store backend. Implements the existing HalStorage.h API;
// never compile together with lib/hal/HalStorage.cpp (the reader SPI backend).
#define HAL_STORAGE_IMPL
#include <Arduino.h>
#include <HalStorage.h>
#include "SdmmcBootstrapDisk.h"
#include "StoragePathPolicy.h"
#include "ff.h"
#include <algorithm>
#include <climits>
#include <cstring>
#include <limits>
#include <new>
#include <atomic>
#include <esp_system.h>

namespace {
constexpr size_t kPathMax = 255, kHandles = 16, kChunk = 4096;
constexpr unsigned kDirectoryItems = 2048;
size_t liveHandles = 0;
std::atomic<bool> poisoned{false};
bool backendMounted = false;
std::atomic<TaskHandle_t> storageOwner{nullptr};
StorageGenerationTracker generations;
bool onOwner() { return storageOwner.load() == xTaskGetCurrentTaskHandle(); }
bool writable(oflag_t flags) { return (flags & O_ACCMODE) != O_RDONLY; }
void mutated() { if (onOwner()) generations.mutationAttempt(); else poisoned = true; }

bool validPath(const char* p) { return BootstrapPathPolicy::valid(p); }
std::string fatPath(const char* p) {
  const char* d = BootstrapSdmmc::drive();
  return d && validPath(p) ? std::string(d) + p : std::string();
}
struct Operation {
  bool ok;
  Operation(uint32_t ms = 2000, uint32_t sectors = 4096)
      : ok(!poisoned && BootstrapSdmmc::beginOperation(ms, sectors)) {}
  ~Operation() { if (ok) BootstrapSdmmc::endOperation(); }
};
bool directory(const char* path) {
  const auto p = fatPath(path);
  if (p.empty()) return false;
  if (!strcmp(path, "/")) return true;
  FILINFO i{};
  const auto result = f_stat(p.c_str(), &i);
  if (result != FR_OK && result != FR_NO_FILE && result != FR_NO_PATH) { poisoned = true; mutated(); }
  return result == FR_OK && (i.fattrib & AM_DIR);
}
}

class HalFile::Impl {
 public:
  FIL file{};
  FF_DIR dir{};
  std::string path;
  FRESULT error = FR_OK;
  oflag_t flags = O_RDONLY;
  bool opened = false, isDir = false;
  unsigned entries = 0;
};

HalStorage HalStorage::instance;
HalStorage::HalStorage() = default;
bool HalStorage::begin() {
  if (backendMounted) return ready();
  TaskHandle_t expected = nullptr;
  if (!storageOwner.compare_exchange_strong(expected, xTaskGetCurrentTaskHandle()) && !onOwner()) return false;
  if (poisoned || !generations.mountAttempt()) return false;
  // This explicitly selected lab profile has one proven wiring/identity. The
  // comparison reads eFuse-derived identity; it does not initialize Wi-Fi.
  const uint8_t allowed[6] = {0x28,0x84,0x85,0x4b,0x57,0x98};
  uint8_t actual[6]{};
  if (esp_read_mac(actual, ESP_MAC_WIFI_STA) != ESP_OK || memcmp(actual, allowed, 6)) return false;
  initialized = BootstrapSdmmc::mount({39, 38, 40}, true) == ESP_OK;
  backendMounted = initialized;
  if (!initialized && BootstrapSdmmc::retained()) poisoned = true;
  generations.mounted(initialized);
  return initialized;
}
bool HalStorage::ready() const {
  return onOwner() && initialized && backendMounted && !poisoned && BootstrapSdmmc::drive();
}
StorageGenerationStamp HalStorage::generation() const {
  return onOwner() ? generations.stamp(ready()) : StorageGenerationStamp{};
}
bool HalStorage::unchanged(const StorageGenerationStamp& stamp) const { return stamp.matches(generation()); }
void HalStorage::invalidateObservations() { mutated(); }
void HalStorage::externalStorageBegin() {
  if (!onOwner()) { poisoned = true; return; }
  // Raw filesystem ownership is not available in this port/profile. Reject
  // coherence permanently rather than guessing that another backend is safe.
  generations.externalBegin(); poisoned = true;
}
void HalStorage::externalStorageEnd(bool closed) {
  if (onOwner()) generations.externalEnd(closed); else poisoned = true;
}
void HalStorage::externalStorageUncertain() {
  if (onOwner()) generations.externalUncertain();
  poisoned = true;
}
bool HalStorage::reconcileExternalStorage() { return ready() && !generations.needsReconcile(); }
void HalStorage::markUnavailable() {
  if (onOwner()) { generations.mutationAttempt(); initialized = false; }
  else poisoned = true;
}

HalFile::HalFile() = default;
HalFile::HalFile(std::unique_ptr<Impl> p) : impl(std::move(p)) {}
HalFile::HalFile(HalFile&&) = default;
HalFile::~HalFile() {
  if (impl && impl->opened && !close()) {
    // Do not pretend failed sync/close released a live FAT handle. Preserve
    // its bounded allocation and quarantine storage until explicit recovery.
    poisoned = true;
    impl.release();
  }
}
HalFile& HalFile::operator=(HalFile&& other) {
  if (this == &other) return *this;
  if (impl && impl->opened && !close()) { poisoned = true; impl.release(); }
  impl = std::move(other.impl);
  return *this;
}
HalFile HalStorage::open(const char* path, oflag_t flags) {
  if (!ready() || !validPath(path) || liveHandles >= kHandles) return {};
  Operation op;
  if (!op.ok) return {};
  const auto p = fatPath(path);
  std::unique_ptr<HalFile::Impl> value(new (std::nothrow) HalFile::Impl);
  if (!value) return {};
  value->path = path;
  value->flags = flags;
  const bool write = (flags & O_ACCMODE) != O_RDONLY;
  constexpr oflag_t supported = O_ACCMODE | O_CREAT | O_EXCL | O_TRUNC | O_APPEND | O_AT_END | O_SYNC;
  if ((flags & ~supported) || (flags & O_ACCMODE) == O_ACCMODE ||
      (!write && (flags & (O_CREAT | O_TRUNC | O_APPEND)))) return {};
  if (write) mutated();
  value->isDir = directory(path);
  if (poisoned) return {};
  if (value->isDir) {
    if (write || (flags & (O_CREAT | O_EXCL | O_TRUNC | O_APPEND))) return {};
    value->error = f_opendir(&value->dir, p.c_str());
  } else {
    BYTE mode = (flags & O_ACCMODE) == O_WRONLY ? FA_WRITE :
                (flags & O_ACCMODE) == O_RDWR ? FA_READ | FA_WRITE : FA_READ;
    if (flags & O_CREAT) {
      const auto policy=BootstrapPathPolicy::creation(true,flags & O_EXCL,flags & O_TRUNC);
      mode |= policy==BootstrapPathPolicy::Create::NewOnly ? FA_CREATE_NEW :
              policy==BootstrapPathPolicy::Create::Replace ? FA_CREATE_ALWAYS : FA_OPEN_ALWAYS;
    } else if (flags & O_TRUNC) {
      // TRUNC without CREATE must not create an absent file.
      FILINFO info{};
      if (f_stat(p.c_str(), &info) != FR_OK) return {};
      mode |= FA_CREATE_ALWAYS;
    }
    value->error = f_open(&value->file, p.c_str(), mode);
  }
  if (value->error != FR_OK) return {};
  value->opened = true;
  ++liveHandles;
  if (!generations.opened(write)) { poisoned = true; value.release(); return {}; }
  if (!value->isDir && (flags & (O_APPEND | O_AT_END))) {
    value->error = f_lseek(&value->file, f_size(&value->file));
    if (value->error != FR_OK) {
      if (f_close(&value->file) == FR_OK) { --liveHandles; generations.closed(write); }
      else { poisoned = true; value.release(); }
      return {};
    }
  }
  return HalFile(std::move(value));
}
bool HalFile::isOpen() const { return onOwner() && !poisoned && impl && impl->opened; }
HalFile::operator bool() const { return isOpen(); }
bool HalFile::isDirectory() const { return isOpen() && impl->isDir; }
uint8_t HalFile::getError() const { return impl ? static_cast<uint8_t>(impl->error) : 0; }
uint64_t HalFile::fileSize64() { return isOpen() && !impl->isDir ? f_size(&impl->file) : 0; }
size_t HalFile::size() { return static_cast<size_t>(std::min<uint64_t>(SIZE_MAX, fileSize64())); }
size_t HalFile::fileSize() { return size(); }
size_t HalFile::position() const { return isOpen() && !impl->isDir ? f_tell(&impl->file) : 0; }
int HalFile::available() const {
  return isOpen() && !impl->isDir ? static_cast<int>(std::min<uint64_t>(INT_MAX,
      f_size(&impl->file) - std::min(f_size(&impl->file), f_tell(&impl->file)))) : 0;
}
size_t HalFile::getName(char* dst, size_t count) {
  if (!isOpen() || !dst || !count) return 0;
  const char* p = strrchr(impl->path.c_str(), '/');
  p = p ? p + 1 : impl->path.c_str();
  if (strlen(p) >= count) { dst[0] = 0; impl->error = FR_INVALID_NAME; return 0; }
  strcpy(dst, p); return strlen(p);
}
bool HalFile::seek64(uint64_t offset) {
  if (!isOpen() || impl->isDir) return false;
  if (offset > std::numeric_limits<FSIZE_t>::max()) { impl->error = FR_INVALID_PARAMETER; return false; }
  Operation op;
  if (!op.ok) { impl->error = FR_TIMEOUT; return false; }
  if (writable(impl->flags)) mutated();
  impl->error = f_lseek(&impl->file, static_cast<FSIZE_t>(offset));
  return impl->error == FR_OK && f_tell(&impl->file) == offset;
}
bool HalFile::seek(size_t p) { return seek64(p); }
bool HalFile::seekSet(size_t p) { return seek64(p); }
bool HalFile::seekCur(int64_t delta) {
  if (!isOpen() || impl->isDir) return false;
  uint64_t offset=0;
  if (!BootstrapPathPolicy::relativeOffset(position(),delta,offset)) {
    impl->error=FR_INVALID_PARAMETER; return false;
  }
  return seek64(offset);
}
int HalFile::read(void* dst, size_t count) {
  if (!isOpen() || impl->isDir) return -1;
  if ((!dst && count) || count > 8u*1024u*1024u) { impl->error=FR_INVALID_PARAMETER; return -1; }
  Operation op(20000,8192);
  if (!op.ok) { impl->error=FR_TIMEOUT; return -1; }
  size_t total=0;
  while (total<count) {
    UINT done=0;
    const size_t amount=std::min(kChunk,count-total);
    impl->error=f_read(&impl->file,static_cast<uint8_t*>(dst)+total,amount,&done);
    total+=done;
    if (impl->error!=FR_OK || done<amount) break;
    vTaskDelay(1);
  }
  return total ? static_cast<int>(total) : impl->error==FR_OK ? 0 : -1;
}
int HalFile::read() { uint8_t c; return read(&c, 1) == 1 ? c : -1; }
size_t HalFile::write(const void* src, size_t count) {
  if (!isOpen() || impl->isDir) return 0;
  if ((!src && count) || count>8u*1024u*1024u) { impl->error=FR_INVALID_PARAMETER; return 0; }
  Operation op(20000,8192);
  if (!op.ok) { impl->error=FR_TIMEOUT; return 0; }
  mutated();
  if (impl->flags & O_APPEND) {
    impl->error=f_lseek(&impl->file,f_size(&impl->file));
    if (impl->error!=FR_OK) return 0;
  }
  size_t total=0;
  while (total<count) {
    UINT done=0;
    const size_t amount=std::min(kChunk,count-total);
    impl->error=f_write(&impl->file,static_cast<const uint8_t*>(src)+total,amount,&done);
    total+=done;
    if (impl->error!=FR_OK || done<amount) break;
    vTaskDelay(1);
  }
  if (impl->error==FR_OK && (impl->flags&O_SYNC)) impl->error=f_sync(&impl->file);
  return total;
}
size_t HalFile::write(uint8_t c) { return write(&c, 1); }
void HalFile::flush() {
  if (!isOpen() || impl->isDir) return;
  Operation op;
  impl->error = op.ok ? f_sync(&impl->file) : FR_TIMEOUT;
}
bool HalFile::close() {
  if (!impl || !impl->opened) return true;
  if (!onOwner() || poisoned) return false;
  Operation op;
  if (!op.ok) { impl->error = FR_TIMEOUT; return false; }
  const FRESULT result = impl->isDir ? f_closedir(&impl->dir) : f_close(&impl->file);
  if (result != FR_OK) { impl->error = result; return false; }
  impl->opened = false;
  --liveHandles;
  generations.closed(writable(impl->flags));
  return true;
}
void HalFile::rewindDirectory() {
  if (!isDirectory()) return;
  Operation op;
  impl->error = op.ok ? f_readdir(&impl->dir, nullptr) : FR_TIMEOUT;
  if (impl->error == FR_OK) impl->entries = 0;
}
HalFile HalFile::openNextFile() {
  if (!isDirectory()) return {};
  std::string child;
  {
    Operation op;
    if (!op.ok || impl->entries >= kDirectoryItems) { impl->error = FR_TIMEOUT; return {}; }
    FILINFO info{};
    impl->error = f_readdir(&impl->dir, &info);
    if (impl->error != FR_OK || !info.fname[0]) return {};
    ++impl->entries;
    child = impl->path + (impl->path == "/" ? "" : "/") + info.fname;
  }
  HalFile result = Storage.open(child.c_str(), O_RDONLY);
  if (!result) impl->error = FR_DISK_ERR; // Not clean end-of-directory.
  return result;
}
bool HalFile::rename(const char* name) {
  if (!isOpen() || !validPath(name)) return false;
  const std::string old = impl->path;
  const uint64_t offset=position();
  const oflag_t flags=impl->flags & (O_ACCMODE|O_APPEND|O_SYNC);
  const bool wasDirectory=impl->isDir;
  // Close before same-volume rename, then restore the live handle semantics.
  if (!close() || !Storage.rename(old.c_str(),name)) return false;
  auto replacement=Storage.open(name,flags);
  if (!replacement || (!wasDirectory && !replacement.seek64(offset))) return false;
  *this=std::move(replacement);
  return true;
}

bool HalStorage::exists(const char* path) {
  if (!ready()) return false;
  const auto p = fatPath(path); if (p.empty()) return false;
  Operation op; if (!op.ok) return false;
  if (!strcmp(path, "/")) return true;
  FILINFO i{}; const auto result = f_stat(p.c_str(), &i);
  if (result != FR_OK && result != FR_NO_FILE && result != FR_NO_PATH) {
    poisoned = true; mutated();
  }
  return result == FR_OK;
}
bool HalStorage::mkdir(const char* path, bool parents) {
  if (!ready() || !validPath(path)) return false;
  Operation op; if (!op.ok) return false;
  mutated();
  auto p = fatPath(path);
  if (parents) {
    for (size_t at = 3; at < p.size(); ++at) if (p[at] == '/') {
      const auto part = p.substr(0, at); FILINFO info{};
      const auto e = f_mkdir(part.c_str());
      if (e != FR_OK && (e != FR_EXIST || f_stat(part.c_str(), &info) != FR_OK || !(info.fattrib & AM_DIR))) return false;
    }
  }
  const auto e = f_mkdir(p.c_str());
  return e == FR_OK || (e == FR_EXIST && directory(path));
}
bool HalStorage::ensureDirectoryExists(const char* path) { return mkdir(path, true); }
bool HalStorage::remove(const char* path) {
  if (!ready()) return false;
  const auto p = fatPath(path); if (p.empty() || !strcmp(path, "/")) return false;
  Operation op; if (!op.ok) return false;
  mutated();
  FILINFO info{};
  return f_stat(p.c_str(), &info) == FR_OK && !(info.fattrib & AM_DIR) && f_unlink(p.c_str()) == FR_OK;
}
bool HalStorage::rmdir(const char* path) {
  if (!ready()) return false;
  const auto p = fatPath(path); if (p.empty() || !strcmp(path, "/")) return false;
  Operation op; if (!op.ok) return false; mutated();
  return directory(path) && f_unlink(p.c_str()) == FR_OK;
}
bool HalStorage::rename(const char* from, const char* to) {
  if (!ready()) return false;
  const auto a = fatPath(from), b = fatPath(to);
  if (a.empty() || b.empty() || !strcmp(from, "/") || !strcmp(to, "/")) return false;
  Operation op; if (!op.ok) return false; mutated();
  return f_rename(a.c_str(), b.c_str()) == FR_OK;
}
bool HalStorage::openFileForRead(const char*, const char* p, HalFile& f) { f = open(p); return f && !f.isDirectory(); }
bool HalStorage::openFileForWrite(const char*, const char* p, HalFile& f) {
  if (!validPath(p)) return false;
  const std::string path(p); const auto slash = path.rfind('/');
  if (slash > 0 && !mkdir(path.substr(0, slash).c_str(), true)) return false;
  f = open(p, O_RDWR | O_CREAT | O_TRUNC); return static_cast<bool>(f);
}
#define PATH_OVERLOAD(method, type) bool HalStorage::method(const char* m, const type& p, HalFile& f) { return method(m,p.c_str(),f); }
PATH_OVERLOAD(openFileForRead, std::string)
PATH_OVERLOAD(openFileForRead, String)
PATH_OVERLOAD(openFileForWrite, std::string)
PATH_OVERLOAD(openFileForWrite, String)

std::vector<String> HalStorage::listFiles(const char* p, int maximum) {
  std::vector<String> result;
  if (maximum <= 0) return result;
  maximum = std::min(maximum, 200);
  auto dir = open(p); if (!dir.isDirectory()) return result;
  const uint32_t start = millis();
  for (unsigned i = 0; i < kDirectoryItems && result.size() < static_cast<size_t>(maximum) && millis()-start < 10000; ++i) {
    auto entry = dir.openNextFile(); if (!entry) break;
    char name[kPathMax+1];
    if (!entry.isDirectory() && entry.getName(name, sizeof(name))) result.emplace_back(name);
    if (!entry.close()) break;
    delay(1);
  }
  return result;
}
size_t HalStorage::readFileToBuffer(const char* path, char* dst, size_t size, size_t limit) {
  if (!dst || !size) return 0;
  dst[0]=0; auto f=open(path); if (!f || f.isDirectory()) return 0;
  const size_t cap=std::min<size_t>(size-1,limit?limit:4*1024*1024);
  size_t done=0; const uint32_t start=millis();
  while (done<cap && millis()-start<30000) {
    const int n=f.read(dst+done,std::min(kChunk,cap-done)); if(n<=0) break;
    done+=n; delay(1);
  }
  dst[done]=0; return done;
}
String HalStorage::readFile(const char* path) {
  auto f=open(path); if(!f || f.isDirectory() || f.fileSize64()>50000) return String();
  String result; if(!result.reserve(f.fileSize64()+1)) return String();
  char chunk[257]; const uint32_t start=millis();
  while(f.available() && millis()-start<10000) {
    const int n=f.read(chunk,256); if(n<=0) return String();
    chunk[n]=0; result.concat(chunk,n); delay(1);
  }
  return f.available()?String():result;
}
bool HalStorage::readFileToStream(const char* path, Print& dst, size_t chunkSize) {
  auto f=open(path); if(!f || f.isDirectory() || f.fileSize64()>4*1024*1024) return false;
  uint8_t chunk[256]; const size_t amount=std::min<size_t>(chunkSize?chunkSize:256,256);
  const uint32_t start=millis();
  while(f.available()) {
    if(millis()-start>=30000) return false;
    const int n=f.read(chunk,amount); if(n<=0 || dst.write(chunk,n)!=static_cast<size_t>(n)) return false;
    delay(1);
  }
  return f.close();
}
bool HalStorage::writeFile(const char* path, const String& text) {
  if(text.length()>4*1024*1024) return false;
  HalFile f; if(!openFileForWrite("bootstrap",path,f)) return false;
  size_t done=0; const uint32_t start=millis();
  while(done<text.length()) {
    if(millis()-start>=30000) return false;
    const size_t n=std::min(kChunk,text.length()-done);
    if(f.write(text.c_str()+done,n)!=n || f.getError()) return false;
    done+=n; delay(1);
  }
  return f.close();
}
bool HalStorage::removeDir(const char* path) {
  if(!validPath(path) || !strcmp(path,"/")) return false;
  struct Frame { std::string path; HalFile dir; } stack[8];
  size_t depth=1; stack[0].path=path; stack[0].dir=open(path);
  if(!stack[0].dir.isDirectory()) return false;
  const uint32_t start=millis();
  for(unsigned items=0;depth && items<512 && millis()-start<10000;++items) {
    auto& frame=stack[depth-1]; auto child=frame.dir.openNextFile();
    if(!child) {
      if(frame.dir.getError() || !frame.dir.close() || !rmdir(frame.path.c_str())) return false;
      --depth;
    } else {
      char name[kPathMax+1]; if(!child.getName(name,sizeof(name))) return false;
      const std::string p=frame.path+"/"+name; const bool isDir=child.isDirectory();
      if(!child.close()) return false;
      if(isDir) {
        if(depth==8) return false;
        stack[depth].path=p; stack[depth].dir=open(p.c_str());
        if(!stack[depth].dir.isDirectory()) return false;
        ++depth;
      } else if(!remove(p.c_str())) return false;
    }
    delay(1);
  }
  return depth==0;
}

namespace BootstrapHalStorage {
unsigned openHandleCount() { return onOwner() ? static_cast<unsigned>(liveHandles) : UINT_MAX; }
bool releaseForHandoff() {
  if (!onOwner() || liveHandles || poisoned) return false;
  if (!backendMounted) return !BootstrapSdmmc::retained();
  if (!BootstrapSdmmc::drive()) return false;
  mutated();
  if (BootstrapSdmmc::unmount()!=ESP_OK) return false;
  backendMounted=false;
  Storage.markUnavailable();
  return true;
}
}
