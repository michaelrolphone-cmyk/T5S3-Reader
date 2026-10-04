#pragma once
#include "Print.h"
extern "C" uint64_t card_time;
inline unsigned long millis() { return static_cast<unsigned long>(card_time); }
inline unsigned hal_delay_calls=0; inline uint64_t hal_delay_requested_ms=0;
inline void delay(unsigned long n) { ++hal_delay_calls; hal_delay_requested_ms += n; card_time += n; }

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
