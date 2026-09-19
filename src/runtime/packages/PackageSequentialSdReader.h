#pragma once

// The ordinary package engine uses fixed-size chunks to keep its stack bounded.
// Reopening and seeking the SD file for every chunk turns a multi-megabyte ELF
// verification into thousands of FAT directory traversals. This adapter holds
// exactly one file handle during each strictly sequential entry read; it never
// caches verified digests or executable bytes across operations.

#include <HalStorage.h>
#include <cstddef>
#include <cstdint>
#include <string>

namespace RuntimePackages {
class OrdinarySequentialSdReader final {
 public:
  OrdinarySequentialSdReader() = default;
  OrdinarySequentialSdReader(const OrdinarySequentialSdReader&) = delete;
  OrdinarySequentialSdReader& operator=(const OrdinarySequentialSdReader&) = delete;
  ~OrdinarySequentialSdReader() { if (file_.isOpen()) (void)file_.close(); }

  bool readAt(const std::string& path, uint64_t offset,
              uint8_t* destination, size_t length) {
    if (path.empty() || !destination || !length) return false;
    if (offset == 0) {
      // A new entry must not inherit a previous entry's open descriptor.
      if (file_.isOpen() && !file_.close()) return false;
      file_ = Storage.open(path.c_str(), O_RDONLY);
      if (!file_.isOpen() || file_.isDirectory()) {
        if (file_.isOpen()) (void)file_.close();
        return false;
      }
      path_ = path;
      size_ = file_.fileSize64();
      next_ = 0;
    }
    // The staging/verifier loops are sequential by contract. Reject a skipped,
    // overlapping, switched or out-of-bounds chunk instead of silently seeking
    // into unrelated data or hashing a different entry.
    if (!file_.isOpen() || path != path_ || offset != next_ ||
        offset > size_ || length > size_ - offset) return false;
    if (file_.read(destination, length) != static_cast<int>(length)) {
      (void)file_.close();
      return false;
    }
    next_ += length;
    // Return a close failure as an integrity failure. The normal path closes
    // on the final chunk; exceptional paths close in the destructor.
    if (next_ == size_) return file_.close();
    return true;
  }

 private:
  HalFile file_;
  std::string path_;
  uint64_t size_ = 0;
  uint64_t next_ = 0;
};
} // namespace RuntimePackages
