#pragma once

#include <cstddef>
#include <cstdint>

// Same-worker termination around the existing Arduino HTTP transport. This is
// not a socket owner on another task, a new HTTP parser, or a TLS policy change.
namespace HttpClientBudget {
class Budget {
 public:
  using Clock = uint32_t (*)();
  using Cooperate = void (*)();
  enum class Failure { None, Idle, Deadline, HeaderLine };
  explicit Budget(Clock clock, uint32_t totalMs = 300000, uint32_t idleMs = 30000, Cooperate cooperate = nullptr)
      : clock_(clock), cooperate_(cooperate), started_(clock()), progress_(started_), total_(totalMs), idle_(idleMs) {}
  bool allow() {
    const auto now = clock_();
    if (failure_ == Failure::None && now - started_ >= total_) failure_ = Failure::Deadline;
    if (failure_ == Failure::None && now - progress_ >= idle_) failure_ = Failure::Idle;
    return failure_ == Failure::None;
  }
  void progress() { progress_ = clock_(); }
  void reject(Failure reason) {
    if (failure_ == Failure::None) failure_ = reason;
  }
  Failure failure() const { return failure_; }
  void cooperate() const {
    if (cooperate_) cooperate_();
  }

 private:
  Clock clock_;
  Cooperate cooperate_;
  uint32_t started_, progress_, total_, idle_;
  Failure failure_ = Failure::None;
};

template <class Base>
class Client final : public Base {
 public:
  explicit Client(Budget& budget) : budget_(budget) {}
  using Base::connect;
  int connect(const char* host, uint16_t port, int32_t timeout) override {
    if (!allowed()) return 0;
    const int result = Base::connect(host, port, timeout);
    // Connect/TLS have separate lower-level bounds. Idle body timing starts
    // only after connection; redirects never reset the total request deadline.
    budget_.progress();
    return allowed() ? result : 0;
  }
  int available() override { return allowed() ? Base::available() : 0; }
  uint8_t connected() override { return allowed() ? Base::connected() : 0; }
  int peek() override { return allowed() ? Base::peek() : -1; }
  int read() override {
    if (!allowed()) return -1;
    uint8_t byte{};
    const int result = Base::read(&byte, 1);
    if (!allowed() || (result > 0 && !observe(&byte, 1))) return -1;
    if (result > 0) budget_.progress();
    return result > 0 ? byte : -1;
  }
  int read(uint8_t* bytes, size_t count) override {
    if (!allowed()) return -1;
    const int result = Base::read(bytes, count);
    if (!allowed() || (result > 0 && !observe(bytes, static_cast<size_t>(result)))) return -1;
    if (result > 0) budget_.progress();
    return result;
  }
  size_t readBytes(char* bytes, size_t count) override {
    if (!allowed()) return 0;
    // HTTPClient uses readBytes for body blocks and readStringUntil/read for
    // header/chunk-size lines. Do not impose a text-line bound on binary data.
    const bool previous = bodyRead_;
    bodyRead_ = true;
    const size_t result = Base::readBytes(bytes, count);
    bodyRead_ = previous;
    return allowed() ? result : 0;
  }
  size_t readBytes(uint8_t* bytes, size_t count) override { return readBytes(reinterpret_cast<char*>(bytes), count); }
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* bytes, size_t count) override {
    if (!allowed()) return 0;
    const size_t result = Base::write(bytes, count);
    if (!allowed()) return 0;
    if (result) budget_.progress();
    return result;
  }

 private:
  bool observe(const uint8_t* bytes, size_t count) {
    if (bodyRead_) return true;
    for (size_t i = 0; i < count; ++i) {
      if (bytes[i] == '\n')
        lineBytes_ = 0;
      else if (++lineBytes_ > 8192) {
        budget_.reject(Budget::Failure::HeaderLine);
        return allowed();
      }
    }
    return true;
  }
  bool allowed() {
    if (budget_.allow()) return true;
    if (!stopped_) {
      stopped_ = true;
      Base::stop();
    }
    // Arduino Stream's timedRead may retry a failed read until its own short
    // timeout. Keep that cleanup tail scheduler-cooperative after revocation.
    budget_.cooperate();
    return false;
  }
  Budget& budget_;
  bool stopped_ = false, bodyRead_ = false;
  uint32_t lineBytes_ = 0;
};
}  // namespace HttpClientBudget
