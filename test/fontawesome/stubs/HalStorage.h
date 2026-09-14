#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <chrono>
class FsFile {
  FILE* file = nullptr;
 public:
  ~FsFile() { close(); }
  bool open(const char* path) { close(); file = std::fopen(path, "rb"); return file != nullptr; }
  int read(uint8_t* buffer, size_t count) { return file ? static_cast<int>(std::fread(buffer, 1, count, file)) : -1; }
  bool seekSet(uint32_t offset) { return file && std::fseek(file, offset, SEEK_SET) == 0; }
  void close() { if (file) std::fclose(file); file = nullptr; }
};
struct TestStorage {
  std::string root;
  bool openFileForRead(const char*, const char* path, FsFile& file) { return file.open((root + path).c_str()); }
};
inline TestStorage Storage;
inline unsigned long millis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
