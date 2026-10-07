#pragma once
// Deterministic storage endpoint fixture; the complete XtcParser is production.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace fixture {
struct Image {
  uint64_t size = 0;
  std::map<uint64_t, uint8_t> bytes;
  void put(uint64_t offset, const void* data, size_t length) {
    const auto* input = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < length; ++i) bytes[offset + i] = input[i];
    size = std::max(size, offset + length);
  }
};
inline std::map<std::string, Image> images;
inline uint64_t failSeek = UINT64_MAX;
inline uint64_t failRead = UINT64_MAX;
inline int readResult = -1;
inline bool failOpen = false;
inline unsigned opens = 0, closes = 0, live = 0;
inline std::vector<uint64_t> seeks;
inline std::vector<std::pair<uint64_t, size_t>> reads;
inline void clearFaults() {
  failSeek = failRead = UINT64_MAX;
  readResult = -1;
  failOpen = false;
  seeks.clear();
  reads.clear();
}
}  // namespace fixture

class FsFile {
 public:
  fixture::Image* image = nullptr;
  uint64_t position = 0;
  bool isOpen() const { return image != nullptr; }
  bool close() {
    if (image) { ++fixture::closes; --fixture::live; image = nullptr; }
    return true;
  }
  bool seek64(uint64_t offset) {
    fixture::seeks.push_back(offset);
    if (!image || offset == fixture::failSeek || offset > image->size) return false;
    position = offset;
    return true;
  }
  bool seek(uint32_t offset) { return seek64(offset); }
  uint64_t fileSize64() const { return image ? image->size : 0; }
  int read(void* output, size_t size) {
    fixture::reads.emplace_back(position, size);
    if (!image) return -1;
    if (position == fixture::failRead) {
      if (fixture::readResult < 0) return fixture::readResult;
      size = std::min(size, static_cast<size_t>(fixture::readResult));
    }
    size = static_cast<size_t>(std::min<uint64_t>(size, image->size - position));
    auto* bytes = static_cast<uint8_t*>(output);
    for (size_t i = 0; i < size; ++i) {
      const auto found = image->bytes.find(position++);
      bytes[i] = found == image->bytes.end() ? 0 : found->second;
    }
    return static_cast<int>(size);
  }
};
struct StorageFixture {
  bool openFileForRead(const char*, const char* path, FsFile& file) {
    if (fixture::failOpen || !fixture::images.count(path)) return false;
    file.close();
    file.image = &fixture::images.at(path);
    file.position = 0;
    ++fixture::opens;
    ++fixture::live;
    return true;
  }
};
inline StorageFixture Storage;
