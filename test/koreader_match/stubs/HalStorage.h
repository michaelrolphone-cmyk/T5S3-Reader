#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>

// Synthetic storage only. No user's files, network or credentials are accessed.
struct String {
  std::string value;
  String() = default;
  explicit String(std::string text) : value(std::move(text)) {}
  bool isEmpty() const { return value.empty(); }
  const char* c_str() const { return value.c_str(); }
  size_t write(uint8_t byte) { value.push_back(static_cast<char>(byte)); return 1; }
  size_t write(const uint8_t* bytes, size_t size) {
    value.append(reinterpret_cast<const char*>(bytes), size);
    return size;
  }
};

inline unsigned fixtureFileOpens = 0;
inline unsigned fixtureFileCloses = 0;
struct FsFile {
  std::string bytes;
  size_t position = 0;
  bool live = false;
  bool failLastByte = false;
  ~FsFile() { if (live) ++fixtureFileCloses; }
  size_t available() const { return bytes.size() - position; }
  size_t read(void* output, size_t size) {
    if (failLastByte && available() == 1) return 0;
    size = std::min(size, available());
    memcpy(output, bytes.data() + position, size);
    position += size;
    return size;
  }
  size_t write(const uint8_t*, size_t size) { return size; }
};

struct FakeStorage {
  std::map<std::string, std::string> files;
  unsigned writes = 0;
  unsigned renames = 0;
  bool failWrite = false;
  bool failOpen = false;
  bool failLastByte = false;
  void mkdir(const char*) {}
  bool exists(const char* path) const { return files.count(path); }
  String readFile(const char* path) { return String{files[path]}; }
  bool writeFile(const char* path, const String& data) {
    ++writes;
    if (failWrite) return false;
    files[path] = data.value;
    return true;
  }
  bool rename(const char* from, const char* to) {
    ++renames;
    files[to] = files[from];
    files.erase(from);
    return true;
  }
  bool openFileForRead(const char*, const char* path, FsFile& file) {
    if (failOpen || !exists(path)) return false;
    file.bytes = files[path];
    file.position = 0;
    file.live = true;
    file.failLastByte = failLastByte;
    ++fixtureFileOpens;
    return true;
  }
};
inline FakeStorage Storage;
