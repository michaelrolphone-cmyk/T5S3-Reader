#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <utility>

#include "Arduino.h"

struct NativeStorageTestFile {
  std::string bytes;
  bool directory = false;
  bool open_allowed = true;
  bool close_allowed = true;
  int max_read_size = 2048;
  int fail_read_call = -1;
  int read_calls = 0;
  int close_calls = 0;
};

class HalFile {
public:
  bool isOpen() const { return is_open_; }
  explicit operator bool() const { return is_open_; }
  bool isDirectory() const { return file_ && file_->directory; }
  uint64_t fileSize64() const { return file_ ? file_->bytes.size() : 0; }
  int read(void* output, size_t count) {
    if (!is_open_ || !file_ || !output) return -1;
    ++file_->read_calls;
    if (file_->fail_read_call == file_->read_calls) return -1;
    if (position_ >= file_->bytes.size()) return 0;
    const size_t amount = std::min({count, file_->bytes.size() - position_,
                                   static_cast<size_t>(file_->max_read_size)});
    std::memcpy(output, file_->bytes.data() + position_, amount);
    position_ += amount;
    return static_cast<int>(amount);
  }
  bool seek64(uint64_t offset) {
    if (!is_open_ || !file_ || offset > file_->bytes.size()) return false;
    position_ = static_cast<size_t>(offset);
    return true;
  }
  bool close() {
    if (is_open_ && file_) ++file_->close_calls;
    const bool success = !file_ || file_->close_allowed;
    is_open_ = false;
    return success;
  }
  size_t write(const void*, size_t size) { return size; }
  void flush() {}
  void open(NativeStorageTestFile* file) { file_ = file; position_ = 0; is_open_ = file && file->open_allowed; }

private:
  NativeStorageTestFile* file_ = nullptr;
  size_t position_ = 0;
  bool is_open_ = false;
};

class HalStorage {
public:
  bool ready() const { return ready_; }
  bool exists(const char* path) const { return path && files_.find(path) != files_.end(); }
  String readFile(const char* path) const {
    auto it = path ? files_.find(path) : files_.end();
    if (it == files_.end()) return String();
    String contents;
    const size_t limit = std::min<size_t>(it->second.bytes.size(), 50000);
    for (size_t i = 0; i < limit; ++i) contents += it->second.bytes[i];
    return contents;
  }
  HalFile open(const char* path, int) {
    HalFile result;
    auto it = path ? files_.find(path) : files_.end();
    result.open(it == files_.end() ? nullptr : &it->second);
    return result;
  }
  bool ensureDirectoryExists(const char*) { return true; }
  bool rename(const char*, const char*) { return true; }
  bool remove(const char*) { return true; }
  bool writeFile(const char*, const String&) { return true; }
  bool openFileForWrite(const char*, const char*, HalFile&) { return false; }
  void setReady(bool ready) { ready_ = ready; }
  NativeStorageTestFile& addFile(const std::string& path, std::string bytes, int maxRead = 2048) {
    auto& file = files_[path];
    file.bytes = std::move(bytes);
    file.max_read_size = maxRead;
    return file;
  }
  NativeStorageTestFile* find(const std::string& path) {
    auto it = files_.find(path);
    return it == files_.end() ? nullptr : &it->second;
  }

private:
  bool ready_ = true;
  std::map<std::string, NativeStorageTestFile> files_;
};

extern HalStorage native_storage_test_storage;
#define Storage native_storage_test_storage
