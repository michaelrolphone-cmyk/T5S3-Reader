#pragma once

#include <cstddef>
#include <cstdint>

// Operation-local cooperation for many small writes, without buffering or
// changing provider requests, mutation invalidation, validation or retry rules.
// Pre/post checkpoints also cover CPU intervals and zero/error write results.
class HalWriteBudget {
 public:
  using Clock = uint32_t (*)();
  using Yield = void (*)();
  HalWriteBudget(Clock clock, Yield yield) : clock_(clock), yield_(yield), lastYield_(clock()) {}
  void afterWrite(size_t bytes) {
    bytes_ += bytes;
    ++writes_;
    checkpoint();
  }
  void checkpoint() {
    if (bytes_ >= 4096 || writes_ >= 32 || static_cast<uint32_t>(clock_() - lastYield_) >= 8) {
      yield_();
      bytes_ = 0;
      writes_ = 0;
      lastYield_ = clock_();
    }
  }

 private:
  Clock clock_;
  Yield yield_;
  size_t bytes_ = 0;
  unsigned writes_ = 0;
  uint32_t lastYield_;
};
