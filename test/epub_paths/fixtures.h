// In-memory parser storage fixtures adapted from epub_guide/guide_test.py.

#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <expat.h>
class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t*, size_t) = 0;
};
struct FsFile {
  std::shared_ptr<std::vector<uint8_t>> data;
  size_t pos = 0;
  bool error = false;
  explicit operator bool() const { return data != nullptr; }
  bool close() { data.reset(); pos = 0; error = false; return true; }
  size_t size() const { return data ? data->size() : 0; }
  bool getError() const { return error; }
  size_t position() const { return pos; }
  bool seek(size_t value) {
    if (!data || value > data->size()) { error = true; return false; }
    pos = value;
    return true;
  }
  bool available() const { return data && pos < data->size(); }
};
struct StorageFixture {
  std::unordered_map<std::string, std::shared_ptr<std::vector<uint8_t>>> files;
  bool openFileForWrite(const char*, const std::string& path, FsFile& file) {
    file.close();
    file.data = files[path] = std::make_shared<std::vector<uint8_t>>();
    return true;
  }
  bool openFileForRead(const char*, const std::string& path, FsFile& file) {
    file.close();
    auto it = files.find(path);
    if (it == files.end()) return false;
    file.data = it->second;
    return true;
  }
  bool exists(const char* path) const { return files.count(path) != 0; }
  void remove(const char* path) { files.erase(path); }
};
inline StorageFixture Storage;
namespace serialization {
template <typename T> void writePod(FsFile& f, const T& value) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
  f.data->insert(f.data->end(), bytes, bytes + sizeof(T));
  f.pos = f.data->size();
}
template <typename T> void readPod(FsFile& f, T& value) {
  if (!f || f.pos + sizeof(T) > f.data->size()) std::abort();
  std::memcpy(&value, f.data->data() + f.pos, sizeof(T));
  f.pos += sizeof(T);
}
inline void writeString(FsFile& f, const std::string& s) {
  if (!f) return;
  writePod(f, static_cast<uint32_t>(s.size()));
  for (char c : s) f.data->push_back(static_cast<uint8_t>(c));
  f.pos = f.data->size();
}
inline void readString(FsFile& f, std::string& s) {
  uint32_t length = 0;
  readPod(f, length);
  if (f.pos + length > f.data->size()) std::abort();
  s.assign(reinterpret_cast<const char*>(f.data->data() + f.pos), length);
  f.pos += length;
}
}
class BookMetadataCache {
 public:
  std::vector<std::string> spine;
  void createSpineEntry(const std::string& href) { spine.push_back(href); }
};
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
