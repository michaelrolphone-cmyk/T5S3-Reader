#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

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

}  // namespace RuntimeMemory
