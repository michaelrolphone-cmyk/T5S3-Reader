#pragma once
#include <string>
using String = std::string;
constexpr int O_RDONLY = 0;
struct HalFile {
  bool isOpen() const { return false; }
  bool isDirectory() const { return false; }
  unsigned fileSize64() const { return 0; }
  int read(void*, size_t) { return -1; }
  bool close() { return true; }
};
struct ManifestTestStorage {
  HalFile open(const char*, int) { return {}; }
  String readFile(const char*) { return {}; }
};
inline ManifestTestStorage Storage;
