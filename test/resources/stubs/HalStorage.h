#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#ifndef O_RDONLY
#define O_RDONLY 0
#endif

struct FakeSdState {
  std::map<std::string, std::vector<uint8_t>> files;
  int opens = 0;
  int closes = 0;
  bool failRead = false;
  bool failClose = false;
};
inline FakeSdState fakeSd;

class HalFile {
 public:
  HalFile() = default;
  HalFile(std::string path, bool valid) : path_(std::move(path)), open_(valid) {}
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  HalFile(HalFile&& other) noexcept { *this = std::move(other); }
  HalFile& operator=(HalFile&& other) noexcept {
    if (this != &other) {
      path_ = std::move(other.path_);
      open_ = other.open_;
      at_ = other.at_;
      other.open_ = false;
    }
    return *this;
  }
  bool isOpen() const { return open_; }
  bool isDirectory() const { return false; }
  uint64_t fileSize64() const { return fakeSd.files.at(path_).size(); }
  int read(void* destination, size_t length) {
    if (!open_ || fakeSd.failRead) return -1;
    const auto& bytes = fakeSd.files.at(path_);
    if (at_ > bytes.size() || length > bytes.size() - at_) return -1;
    std::memcpy(destination, bytes.data() + at_, length);
    at_ += length;
    return static_cast<int>(length);
  }
  bool close() {
    if (!open_) return false;
    open_ = false;
    ++fakeSd.closes;
    return !fakeSd.failClose;
  }
 private:
  std::string path_;
  bool open_ = false;
  size_t at_ = 0;
};

struct FakeStorage {
  HalFile open(const char* path, int) {
    ++fakeSd.opens;
    return HalFile(path, fakeSd.files.count(path) != 0);
  }
};
inline FakeStorage Storage;
