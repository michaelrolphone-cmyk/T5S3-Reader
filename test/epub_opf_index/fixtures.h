#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Only the platform/file and metadata-output boundaries are fixtures. Production
// parser, XML callbacks, serialization, and path normalization are compiled intact.
class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t*, size_t) = 0;
};

struct IoStats {
  size_t reads = 0;
  size_t readBytes = 0;
  size_t writes = 0;
  size_t writeBytes = 0;
  size_t seeks = 0;
  size_t nonzeroSeeks = 0;
  size_t opens = 0;
  size_t closes = 0;
  size_t closeCalls = 0;
  size_t liveHandles = 0;
  size_t peakHandles = 0;
  size_t removed = 0;
  std::vector<size_t> seekOffsets;
};

struct Faults {
  size_t partialWriteCall = 0;
  size_t partialWriteBytes = 0;
  size_t failedCloseCall = 0;
  size_t failedNonzeroSeek = 0;
  bool failReadOpen = false;
  bool failWriteOpen = false;
  bool unavailableReads = false;
  bool failAllSeeks = false;
  bool poisonFailedSeek = false;
  bool poisonedSeekCanMove = false;
  size_t reportedExtraSize = 0;
  size_t errorAfterWriteCall = 0;
  bool errorOnReadOpen = false;
};

inline IoStats io;
inline Faults faults;

[[noreturn]] inline void fixtureFailure(const char* message) {
  std::fprintf(stderr, "Fixture failure: %s\n", message);
  std::abort();
}

class FsFile {
 public:
  std::shared_ptr<std::vector<uint8_t>> data;
  size_t cursor = 0;
  bool writing = false;
  uint8_t error = 0;

  FsFile() = default;
  FsFile(const FsFile&) = delete;
  FsFile& operator=(const FsFile&) = delete;
  ~FsFile() { release(); }

  explicit operator bool() const { return data != nullptr; }

  void release() {
    if (data) {
      ++io.closes;
      --io.liveHandles;
      data.reset();
    }
    cursor = 0;
  }

  void attach(const std::shared_ptr<std::vector<uint8_t>>& bytes, bool writer) {
    release();
    data = bytes;
    writing = writer;
    error = (!writer && faults.errorOnReadOpen) ? 1 : 0;
    ++io.opens;
    ++io.liveHandles;
    io.peakHandles = std::max(io.peakHandles, io.liveHandles);
  }

  bool close() {
    ++io.closeCalls;
    if (faults.failedCloseCall == io.closeCalls) return false;
    release();
    return true;
  }

  uint8_t getError() const { return error; }
  size_t position() const { return cursor; }
  size_t size() const { return data ? data->size() + faults.reportedExtraSize : 0; }
  bool available() const { return data && !faults.unavailableReads && cursor < data->size(); }

  bool seek(size_t offset) {
    ++io.seeks;
    io.seekOffsets.push_back(offset);
    if (offset != 0) {
      ++io.nonzeroSeeks;
      if (faults.failedNonzeroSeek == io.nonzeroSeeks) {
        if (faults.poisonFailedSeek) error = 1;
        return false;
      }
    }
    if (faults.failAllSeeks || (error && faults.poisonFailedSeek && !faults.poisonedSeekCanMove) ||
        !data || offset > data->size()) return false;
    cursor = offset;
    return true;
  }

  int read(void* destination, size_t count) {
    ++io.reads;
    // Serialization::readString has no error result and does not initialize a
    // failed length read. Never fabricate safe output for that undefined path.
    if (!data || faults.unavailableReads || (error && faults.poisonFailedSeek) ||
        cursor > data->size() || count > data->size() - cursor) {
      fixtureFailure("unexpected unchecked short read; no corrupt-record oracle");
    }
    if (count) std::memcpy(destination, data->data() + cursor, count);
    cursor += count;
    io.readBytes += count;
    return static_cast<int>(count);
  }

  size_t write(const uint8_t* source, size_t count) {
    ++io.writes;
    if (faults.errorAfterWriteCall == io.writes) error = 1;
    if (!data) return 0;
    if (!writing) fixtureFailure("write to read-only fixture handle");
    if (faults.partialWriteCall == io.writes) count = std::min(count, faults.partialWriteBytes);
    if (cursor + count > data->size()) data->resize(cursor + count);
    if (count) std::memcpy(data->data() + cursor, source, count);
    cursor += count;
    io.writeBytes += count;
    return count;
  }
};

struct StorageFixture {
  std::unordered_map<std::string, std::shared_ptr<std::vector<uint8_t>>> files;
  std::vector<uint8_t> lastRemovedBytes;

  bool openFileForWrite(const char*, const std::string& path, FsFile& file) {
    file.release();
    if (faults.failWriteOpen) return false;
    auto bytes = std::make_shared<std::vector<uint8_t>>();
    files[path] = bytes;
    file.attach(bytes, true);
    return true;
  }

  bool openFileForRead(const char*, const std::string& path, FsFile& file) {
    file.release();
    if (faults.failReadOpen) return false;
    const auto found = files.find(path);
    if (found == files.end()) return false;
    file.attach(found->second, false);
    return true;
  }

  bool exists(const char* path) const { return files.count(path) != 0; }
  bool remove(const char* path) {
    const auto found = files.find(path);
    if (found == files.end()) return false;
    lastRemovedBytes = *found->second;
    files.erase(found);
    ++io.removed;
    return true;
  }

  void reset() {
    if (io.liveHandles || !files.empty()) fixtureFailure("previous test leaked a handle or temporary file");
    io = {};
    faults = {};
    lastRemovedBytes.clear();
  }
};

inline StorageFixture Storage;

class BookMetadataCache {
 public:
  std::vector<std::string> spine;
  void createSpineEntry(const std::string& href) { spine.push_back(href); }
};

namespace FsHelpers {
std::string normalisePath(const std::string& path);
}

#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
