#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

// ArduinoJson writer for the settings response. A setting can contain all 128
// SD font names; its size must not be limited to one HTTP output chunk.
template <typename Server>
class SettingsJsonWriter {
 public:
  explicit SettingsJsonWriter(Server& server) : server_(server), started_(millis()) {}

  size_t write(uint8_t value) { return write(&value, 1); }

  size_t write(const uint8_t* data, size_t size) {
    if (failed_) return 0;
    // More than the maximum 130 filesystem-length names even if every byte
    // needs six-byte JSON escaping, with room for the other settings.
    if (size > kMaxBytes - total_) {
      abort();
      return 0;
    }
    size_t copied = 0;
    while (copied < size) {
      const size_t count = std::min(size - copied, sizeof(buffer_) - used_);
      memcpy(buffer_ + used_, data + copied, count);
      used_ += count;
      copied += count;
      total_ += count;
      if (used_ == sizeof(buffer_) && !flush()) return 0;
    }
    return size;
  }

  bool flush() {
    if (!check()) return false;
    // An empty sendContent call is the HTTP chunk terminator, not a flush.
    if (used_ == 0) return true;
    server_.sendContent(buffer_, used_);
    used_ = 0;
    // The server applies its existing bounded response socket timeout. Yield
    // after each at-most-512-byte write, including a slow partial final chunk.
    vTaskDelay(1);
    return check();
  }

  void abort() {
    if (failed_) return;
    failed_ = true;
    used_ = 0;
    server_.abortResponse();
  }

 private:
  bool check() {
    if (failed_) return false;
    if (static_cast<uint32_t>(millis() - started_) >= 15000 || !server_.client().connected()) {
      abort();
      return false;
    }
    return true;
  }

  static constexpr size_t kMaxBytes = 256 * 1024;
  Server& server_;
  const uint32_t started_;
  char buffer_[512];
  size_t used_ = 0;
  size_t total_ = 0;
  bool failed_ = false;
};
