#pragma once
#include <Arduino.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>
struct FsFile;
struct HalStorage {
  static HalStorage& getInstance() { static HalStorage storage; return storage; }
  std::map<std::string, std::string> files;
  std::vector<std::string> events;
  bool openFails = false, readFails = false, shortRead = false, readError = false;
  bool closeFails = false, saveFails = false, renameFails = false;
  unsigned openHandles = 0, saves = 0, renames = 0;
  bool exists(const char* p) const { return files.count(p); }
  bool ensureDirectoryExists(const char*) { return true; }
  String readFile(const char* p) const { auto i = files.find(p); return i == files.end() ? String{} : String{i->second}; }
  bool remove(const char* p) { return files.erase(p); }
  bool rename(const char* oldPath, const char* newPath) {
    ++renames; events.push_back("rename");
    if (renameFails || openHandles || !files.count(oldPath) || files.count(newPath)) return false;
    files[newPath] = files[oldPath]; files.erase(oldPath); return true;
  }
  bool openFileForRead(const char*, const char*, FsFile&);
};
#define Storage HalStorage::getInstance()
struct FsFile {
  std::string bytes;
  size_t position = 0;
  bool opened = false;
  ~FsFile() { if (opened) { opened = false; --Storage.openHandles; } }
  int read(void* target, size_t count) {
    if (!opened || Storage.readFails) return -1;
    count = std::min(count, bytes.size() - position);
    if (Storage.shortRead && count > 0) --count;
    std::memcpy(target, bytes.data() + position, count); position += count;
    Storage.events.push_back("read"); return static_cast<int>(count);
  }
  size_t write(const void*, size_t size) { return size; }
  size_t size() const { return bytes.size(); }
  uint8_t getError() const { return Storage.readError; }
  bool close() { Storage.events.push_back("close"); if (opened) { opened = false; --Storage.openHandles; } return !Storage.closeFails; }
};
inline bool HalStorage::openFileForRead(const char*, const char* p, FsFile& file) {
  events.push_back("open");
  if (openFails || !files.count(p)) return false;
  file.bytes = files[p]; file.opened = true; ++openHandles; return true;
}
