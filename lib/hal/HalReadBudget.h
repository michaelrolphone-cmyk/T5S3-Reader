#pragma once

#include <cstddef>
#include <cstdint>

// Operation-local cooperation for a sequence of small reads. This changes only
// scheduling, never request sizes, provider calls, validation or retry behavior.
// The caller also checkpoints its CPU work (e.g. each rendered image row).
class HalReadBudget {
 public:
  using Clock = uint32_t (*)();
  using Yield = void (*)();
  HalReadBudget(Clock clock, Yield yield) : clock_(clock), yield_(yield), lastYield_(clock()) {}
  void afterRead(size_t bytes) {
    bytes_ += bytes;
    ++reads_;
    checkpoint();
  }
  void afterRow() {
    ++rows_;
    checkpoint();
  }
  void checkpoint() {
    if (bytes_ >= 4096 || reads_ >= 32 || rows_ >= 32 || static_cast<uint32_t>(clock_() - lastYield_) >= 8) {
      yield_();
      bytes_ = reads_ = 0;
      rows_ = 0;
      lastYield_ = clock_();
    }
  }

 private:
  Clock clock_;
  Yield yield_;
  size_t bytes_ = 0;
  unsigned reads_ = 0;
  unsigned rows_ = 0;
  uint32_t lastYield_;
};
