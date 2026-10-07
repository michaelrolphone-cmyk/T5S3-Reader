#pragma once
#include "Arduino.h"
#include "Print.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
struct Io {
  uint64_t reads = 0, bytes = 0, seeks = 0, writes = 0, opens = 0, closes = 0;
  unsigned live = 0;
};
inline std::unordered_map<std::string, Io> io;
inline uint64_t yields = 0;
inline uint32_t readTickCost = 0;
inline int failReadAt = -1, shortReadAt = -1, failSeekAt = -1;
inline void vTaskDelay(unsigned n) { ++yields; ticks += n; }
class FsFile {
  std::shared_ptr<std::vector<uint8_t>> data;
  std::string path;
  size_t pos = 0;
 public:
  ~FsFile() { close(); }
  explicit operator bool() const { return bool(data); }
  void bind(const std::string& name, std::shared_ptr<std::vector<uint8_t>> bytes) {
    close(); data = bytes; path = name; pos = 0;
    ++io[path].opens; ++io[path].live;
  }
  bool close() {
    if (data) { ++io[path].closes; --io[path].live; }
    data.reset(); pos = 0; return true;
  }
  size_t position() const { return pos; }
  bool seek(size_t value) {
    if (!data || value > data->size()) return false;
    ++io[path].seeks;
    if (path == "/cache/spine.bin.tmp" && failSeekAt > 0 && int(io[path].seeks) == failSeekAt) {
      failSeekAt = -1; return false;
    }
    pos = value; return true;
  }
  bool available() const { return data && pos < data->size(); }
  int read(void* out, size_t count) {
    assert(data); ++io[path].reads;
    if (path == "/cache/spine.bin.tmp") {
      ticks += readTickCost;
      if (failReadAt > 0 && int(io[path].reads) == failReadAt) { failReadAt = -1; return -1; }
      if (shortReadAt > 0 && int(io[path].reads) == shortReadAt) { shortReadAt = -1; count /= 2; }
    }
    count = std::min(count, data->size() - pos);
    std::memcpy(out, data->data() + pos, count);
    pos += count; io[path].bytes += count; return int(count);
  }
  size_t write(const void* value, size_t count) {
    assert(data); ++io[path].writes;
    if (pos + count > data->size()) data->resize(pos + count);
    std::memcpy(data->data() + pos, value, count); pos += count; return count;
  }
};
struct StorageFixture {
  std::unordered_map<std::string, std::shared_ptr<std::vector<uint8_t>>> files;
  unsigned failReadOpens = 0, failWriteOpens = 0;
  bool openFileForWrite(const char*, const std::string& path, FsFile& f) {
    f.close(); if (failWriteOpens) { --failWriteOpens; return false; }
    f.bind(path, files[path] = std::make_shared<std::vector<uint8_t>>()); return true;
  }
  bool openFileForRead(const char*, const std::string& path, FsFile& f) {
    f.close(); if (failReadOpens) { --failReadOpens; return false; }
    auto it = files.find(path); if (it == files.end()) return false;
    f.bind(path, it->second); return true;
  }
  bool exists(const char* p) const { return files.count(p); }
  bool remove(const char* p) { return files.erase(p) > 0; }
};
inline StorageFixture Storage;
