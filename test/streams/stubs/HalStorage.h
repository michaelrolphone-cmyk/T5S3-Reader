#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "Arduino.h"
#include "freertos/semphr.h"
enum class TestStorageOperation { Ready, Open, Metadata, Read, Write, Seek, Finish, Destroy, Remove };
inline void (*testStorageHook)(TestStorageOperation) = nullptr;
inline void testStorageIo(TestStorageOperation op) {
  assert(testStreamMutexDepth == 0);
  if (testStorageHook) testStorageHook(op);
}
enum { O_RDONLY = 1, O_WRONLY = 2, O_CREAT = 4, O_EXCL = 8 };
struct TestFile { std::vector<uint8_t> data; bool directory = false; };
extern std::map<std::string, std::shared_ptr<TestFile>> files;
extern bool storageReady, closeOk;
class HalFile {
  std::shared_ptr<TestFile> f;
  size_t position = 0;
 public:
  HalFile() = default;
  explicit HalFile(std::shared_ptr<TestFile> file) : f(file) {}
  ~HalFile() { if (f) testStorageIo(TestStorageOperation::Destroy); }
  bool isOpen() const { return !!f; }
  explicit operator bool() const { return isOpen(); }
  bool isDirectory() { testStorageIo(TestStorageOperation::Metadata); return f->directory; }
  uint64_t fileSize64() { testStorageIo(TestStorageOperation::Metadata); return f->data.size(); }
  bool seek64(uint64_t p) { testStorageIo(TestStorageOperation::Seek); if (!f || p > f->data.size()) return false; position = p; return true; }
  int read(void* out, size_t n) {
    testStorageIo(TestStorageOperation::Read);
    n = std::min(n, f->data.size() - position);
    memcpy(out, f->data.data() + position, n); position += n; return n;
  }
  size_t write(const void* data, size_t n) {
    testStorageIo(TestStorageOperation::Write);
    auto* p = static_cast<const uint8_t*>(data); f->data.insert(f->data.end(), p, p + n); return n;
  }
  bool close() { testStorageIo(TestStorageOperation::Finish); f.reset(); return closeOk; }
};
class HalStorage {
 public:
  bool ready() { testStorageIo(TestStorageOperation::Ready); return storageReady; }
  HalFile open(const char* path, int flags) {
    testStorageIo(TestStorageOperation::Open);
    if ((flags & O_EXCL) && files.count(path)) return {};
    if (!files.count(path)) { if (!(flags & O_CREAT)) return {}; files[path] = std::make_shared<TestFile>(); }
    return HalFile(files[path]);
  }
  bool remove(const char* path) { testStorageIo(TestStorageOperation::Remove); return files.erase(path); }
};
extern HalStorage Storage;
