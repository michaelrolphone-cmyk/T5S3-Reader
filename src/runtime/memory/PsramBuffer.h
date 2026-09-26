#pragma once

#include <esp_heap_caps.h>
#include <cstddef>
#include <cstring>
#include <utility>

namespace RuntimeMemory {

// Explicit bulk-scratch allocation. Do not silently fall back to internal RAM:
// callers use this specifically to preserve contiguous internal heap for TLS,
// task stacks, DMA and hardware-facing allocations.
class PsramBuffer {
 public:
  PsramBuffer() = default;
  explicit PsramBuffer(size_t bytes, bool zero = true) { allocate(bytes, zero); }
  ~PsramBuffer() { reset(); }

  PsramBuffer(const PsramBuffer&) = delete;
  PsramBuffer& operator=(const PsramBuffer&) = delete;

  PsramBuffer(PsramBuffer&& other) noexcept
      : data_(std::exchange(other.data_, nullptr)),
        size_(std::exchange(other.size_, 0)) {}
  PsramBuffer& operator=(PsramBuffer&& other) noexcept {
    if (this != &other) {
      reset();
      data_ = std::exchange(other.data_, nullptr);
      size_ = std::exchange(other.size_, 0);
    }
    return *this;
  }

  bool allocate(size_t bytes, bool zero = true) {
    reset();
    if (!bytes) return false;
    const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    data_ = static_cast<unsigned char*>(
        zero ? heap_caps_calloc(1, bytes, caps) : heap_caps_malloc(bytes, caps));
    if (!data_) return false;
    size_ = bytes;
    return true;
  }

  void reset() {
    if (data_) heap_caps_free(data_);
    data_ = nullptr;
    size_ = 0;
  }

  unsigned char* data() { return data_; }
  const unsigned char* data() const { return data_; }
  char* chars() { return reinterpret_cast<char*>(data_); }
  const char* chars() const { return reinterpret_cast<const char*>(data_); }
  size_t size() const { return size_; }
  explicit operator bool() const { return data_ != nullptr; }

 private:
  unsigned char* data_ = nullptr;
  size_t size_ = 0;
};

}  // namespace RuntimeMemory
