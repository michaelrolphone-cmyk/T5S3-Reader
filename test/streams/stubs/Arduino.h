#pragma once
#include <cstddef>
#include <cstdint>
class Stream {
 public:
  virtual ~Stream() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t*, size_t) = 0;
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  virtual void flush() = 0;
};
extern uint32_t fakeTime;
inline uint32_t millis() { return fakeTime; }
inline void delay(uint32_t n) { fakeTime += n; }
