#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <HalReadBudget.h>
struct TestStorage;
class FsFile {
 public:
  FsFile() = default;
  FsFile(const FsFile&) = delete;
  FsFile& operator=(const FsFile&) = delete;
  ~FsFile() { close(); }
  int read(void* out, size_t count);
  int readCooperatively(void* out, size_t count, HalReadBudget& budget) { int n=read(out,count); budget.afterRead(n > 0 ? n : 0); return n; }
  size_t write(const uint8_t*, size_t) { return 0; }
  explicit operator bool() const { return data != nullptr; }
  size_t size() const { return data ? data->size() : 0; }
  void close();
  TestStorage* owner = nullptr;
  const std::vector<uint8_t>* data = nullptr;
  size_t position = 0;
};
struct TestStorage {
  std::map<std::string, std::vector<uint8_t>> files;
  std::set<std::string> failOpen;
  unsigned openFiles = 0;
  bool remove(const char* path) { return files.erase(path) != 0; }
  bool exists(const char* path) const { return files.count(path) != 0; }
  bool openFileForRead(const char*, const std::string& path, FsFile& file) {
    file.close();
    const auto it = files.find(path);
    if (it == files.end() || failOpen.count(path)) return false;
    file.owner = this; file.data = &it->second; file.position = 0; ++openFiles;
    return true;
  }
};
extern TestStorage Storage;
inline int FsFile::read(void* out, size_t count) {
  if (!data) return -1;
  const auto actual = std::min(count, data->size() - position);
  std::memcpy(out, data->data() + position, actual);
  position += actual;
  return static_cast<int>(actual);
}
inline void FsFile::close() {
  if (owner) --owner->openFiles;
  owner = nullptr; data = nullptr; position = 0;
}
