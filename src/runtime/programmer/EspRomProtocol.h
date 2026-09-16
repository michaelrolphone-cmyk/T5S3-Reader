#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace EspRomProtocol {
constexpr size_t kBlock = 1024;
constexpr size_t kCommandBytes = 8 + 16 + kBlock;
constexpr size_t kFramedBytes = 2 * kCommandBytes + 2;
constexpr size_t kReplyBytes = 192;

inline void le32(uint8_t* p, uint32_t value) {
  p[0] = static_cast<uint8_t>(value);
  p[1] = static_cast<uint8_t>(value >> 8);
  p[2] = static_cast<uint8_t>(value >> 16);
  p[3] = static_cast<uint8_t>(value >> 24);
}
inline uint16_t le16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | static_cast<uint16_t>(p[1]) << 8);
}
inline uint8_t checksum(const uint8_t* data, size_t count) {
  uint8_t result = 0xef;
  for (size_t i = 0; i < count; ++i) result ^= data[i];
  return result;
}
inline size_t encode(uint8_t op, const uint8_t* payload, size_t count, uint32_t check,
                     uint8_t* raw, size_t rawCapacity, uint8_t* framed, size_t framedCapacity) {
  if (!raw || !framed || count > UINT16_MAX || count + 8 > rawCapacity ||
      (count && !payload) || framedCapacity < 2) return 0;
  raw[0] = 0; raw[1] = op;
  raw[2] = static_cast<uint8_t>(count);
  raw[3] = static_cast<uint8_t>(count >> 8);
  le32(raw + 4, check);
  if (count) std::memcpy(raw + 8, payload, count);
  size_t written = 0;
  framed[written++] = 0xc0;
  for (size_t i = 0; i < count + 8; ++i) {
    uint8_t c = raw[i];
    if (c == 0xc0 || c == 0xdb) {
      if (written + 2 >= framedCapacity) return 0;
      framed[written++] = 0xdb;
      framed[written++] = c == 0xc0 ? 0xdc : 0xdd;
    } else {
      if (written + 1 >= framedCapacity) return 0;
      framed[written++] = c;
    }
  }
  framed[written++] = 0xc0;
  return written;
}

class Decoder {
 public:
  enum class Result { More, Frame, Invalid };
  explicit Decoder(size_t max = kReplyBytes) : max_(max <= kReplyBytes ? max : kReplyBytes) {}
  Result feed(uint8_t c) {
    if (!started_) {
      if (c == 0xc0) { started_ = true; length_ = 0; escaped_ = false; }
      return Result::More;
    }
    if (c == 0xc0) {
      if (escaped_) return Result::Invalid;
      if (length_) return Result::Frame;
      return Result::More;
    }
    if (escaped_) {
      if (c != 0xdc && c != 0xdd) return Result::Invalid;
      c = c == 0xdc ? 0xc0 : 0xdb;
      escaped_ = false;
    } else if (c == 0xdb) {
      escaped_ = true;
      return Result::More;
    }
    if (length_ >= max_) return Result::Invalid;
    bytes_[length_++] = c;
    return Result::More;
  }
  void reset() { length_ = 0; started_ = false; escaped_ = false; }
  const uint8_t* data() const { return bytes_; }
  size_t size() const { return length_; }
 private:
  uint8_t bytes_[kReplyBytes]{};
  size_t max_, length_ = 0;
  bool started_ = false, escaped_ = false;
};

inline bool success(uint8_t op, const uint8_t* reply, size_t length,
                    uint8_t* status, uint8_t* error) {
  if (status) *status = 0xff;
  if (error) *error = 0xff;
  if (!reply || length < 12 || reply[0] != 1 || reply[1] != op) return false;
  const size_t data = le16(reply + 2);
  if (data < 4 || data > length - 8) return false;
  const size_t statusOffset = 8 + data - 4;
  if (status) *status = reply[statusOffset];
  if (error) *error = reply[statusOffset + 1];
  return reply[statusOffset] == 0;
}
} // namespace EspRomProtocol
