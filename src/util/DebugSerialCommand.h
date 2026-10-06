#pragma once

#include <cctype>
#include <cstdint>

// The diagnostic console has one command. Recognize it without retaining an
// unbounded line or waiting for another byte on the foreground UI task.
class DebugSerialCommand {
 public:
  static constexpr unsigned MAX_BYTES_PER_POLL = 64;
  static constexpr uint32_t MAX_POLL_MS = 2;

  // Stream::read() must be nonblocking (as it is for the console's HWCDC).
  // A poll consumes at most one line, like the old readStringUntil call. The
  // caller then services input/idle work and the existing loop scheduler yield.
  template <typename Stream, typename Clock>
  bool poll(Stream& stream, Clock clock) {
    const uint32_t start = clock();
    // Complete an expired partial line before admitting a later fragment or
    // retry. Queue bytes have no arrival timestamps: timeouts are observed on
    // foreground polls, with the same configured inter-byte timeout value.
    if (pending && !drainingQueued && static_cast<uint32_t>(start - lastByteAt) >= stream.getTimeout()) return finish();
    drainingQueued = false;
    for (unsigned count = 0; count < MAX_BYTES_PER_POLL; ++count) {
      if (static_cast<uint32_t>(clock() - start) >= MAX_POLL_MS) break;
      if (stream.available() <= 0) break;
      const int value = stream.read();
      if (value < 0) break;  // Queue changed after available(); retry next loop.
      lastByteAt = clock();
      pending = true;
      if (value == '\n') return finish();
      append(static_cast<unsigned char>(value));
    }
    // Retain Stream's inter-byte timeout and partial-line completion, but do
    // not expire a line while bytes remain queued after the work checkpoint.
    drainingQueued = stream.available() > 0;
    if (pending && !drainingQueued &&
        static_cast<uint32_t>(clock() - lastByteAt) >= stream.getTimeout()) {
      return finish();
    }
    return false;
  }

 private:
  enum class Phase : uint8_t { Prefix, LeadingSpace, Command, TrailingSpace, NulTerminated, Invalid };
  Phase phase = Phase::Prefix;
  uint8_t matched = 0;
  uint32_t lastByteAt = 0;
  bool pending = false;
  bool drainingQueued = false;

  void append(const unsigned char value) {
    static constexpr char prefix[] = "CMD:";
    static constexpr char command[] = "SCREENSHOT";
    switch (phase) {
      case Phase::Prefix:
        if (value != prefix[matched]) {
          phase = Phase::Invalid;
        } else if (++matched == sizeof(prefix) - 1) {
          matched = 0;
          phase = Phase::LeadingSpace;
        }
        break;
      case Phase::LeadingSpace:
        if (std::isspace(value)) break;
        phase = Phase::Command;
        [[fallthrough]];
      case Phase::Command:
        if (matched < sizeof(command) - 1) {
          if (value != command[matched]) {
            phase = Phase::Invalid;
          } else {
            ++matched;
          }
        } else if (value == 0) {
          // Arduino String's equals(const char*) uses strcmp, including this
          // existing embedded-NUL behavior. Whitespace before NUL is not trim.
          phase = Phase::NulTerminated;
        } else {
          phase = std::isspace(value) ? Phase::TrailingSpace : Phase::Invalid;
        }
        break;
      case Phase::TrailingSpace:
        if (!std::isspace(value)) phase = Phase::Invalid;
        break;
      case Phase::NulTerminated:
      case Phase::Invalid:
        break;
    }
  }

  bool finish() {
    const bool screenshot = (phase == Phase::Command && matched == 10) || phase == Phase::TrailingSpace ||
                            phase == Phase::NulTerminated;
    phase = Phase::Prefix;
    matched = 0;
    pending = false;
    drainingQueued = false;
    return screenshot;
  }
};
