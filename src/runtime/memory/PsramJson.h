#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <cstdint>
#include <cstring>

#include "runtime/memory/PsramBuffer.h"

namespace RuntimeMemory {

// ArduinoJson allocator that never falls back to scarce internal RAM.
// Catalog JSON is metadata scratch and must not compete with TLS/task/DMA memory.
class PsramJsonAllocator final : public ArduinoJson::Allocator {
 public:
  void* allocate(size_t size) override {
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }

  void deallocate(void* pointer) override {
    if (pointer) heap_caps_free(pointer);
  }

  void* reallocate(void* pointer, size_t newSize) override {
    return heap_caps_realloc(pointer, newSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
};

// Fixed-capacity Stream backed explicitly by PSRAM. Useful for bounded metadata
// downloads where std::string growth would otherwise fragment internal heap.
class PsramTextStream final : public Stream {
 public:
  explicit PsramTextStream(size_t capacity)
      : buffer_(capacity + 1, false), capacity_(capacity) {}

  size_t write(uint8_t byte) override { return write(&byte, 1); }

  size_t write(const uint8_t* data, size_t size) override {
    if (!data || !buffer_ || failed_) return 0;
    if (size > capacity_ - length_) {
      failed_ = true;
      return 0;
    }
    std::memcpy(buffer_.data() + length_, data, size);
    length_ += size;
    buffer_.data()[length_] = 0;
    return size;
  }

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

  bool good() const { return buffer_ && !failed_; }
  bool empty() const { return length_ == 0; }
  const char* chars() const { return buffer_ ? buffer_.chars() : nullptr; }
  size_t size() const { return length_; }

 private:
  PsramBuffer buffer_;
  size_t capacity_ = 0;
  size_t length_ = 0;
  bool failed_ = false;
};

// Grow metadata in PSRAM as needed. Allocation failure stops the HTTP transfer
// cleanly; no fixed catalog byte ceiling or fallback into internal TLS memory.
class PsramGrowingTextStream final : public Stream {
 public:
  PsramGrowingTextStream() = default;
  ~PsramGrowingTextStream() override { if (data_) heap_caps_free(data_); }
  PsramGrowingTextStream(const PsramGrowingTextStream&) = delete;
  PsramGrowingTextStream& operator=(const PsramGrowingTextStream&) = delete;

  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* bytes, size_t count) override {
    if (!bytes || failed_ || count > SIZE_MAX - length_ - 1) {
      failed_ = true;
      return 0;
    }
    const size_t required = length_ + count + 1;
    if (required > capacity_) {
      size_t next = capacity_ ? capacity_ : 4096;
      while (next < required) {
        if (next > SIZE_MAX / 2) { next = required; break; }
        next *= 2;
      }
      void* grown = heap_caps_realloc(data_, next, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (!grown) { failed_ = true; return 0; }
      data_ = static_cast<char*>(grown);
      capacity_ = next;
    }
    std::memcpy(data_ + length_, bytes, count);
    length_ += count;
    data_[length_] = '\0';
    return count;
  }

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

  bool good() const { return !failed_; }
  bool empty() const { return length_ == 0; }
  const char* chars() const { return data_; }
  size_t size() const { return length_; }

 private:
  char* data_ = nullptr;
  size_t length_ = 0;
  size_t capacity_ = 0;
  bool failed_ = false;
};

}  // namespace RuntimeMemory
