#pragma once
#include "runtime/streams/StreamRuntime.h"
#include <cstdint>
#include <cstring>

// Firmware-internal binding between an installed USB class ELF and published
// serial.port endpoints. The class ELF owns transfers. This session never
// calls T5UsbApi and never stores ELF function pointers inside StreamRuntime.
namespace RuntimeUsb {

struct ClassPort {
  void* context = nullptr;
  uint64_t (*open)(void*, uint64_t device) = nullptr;
  bool (*configure)(void*, uint64_t token, uint32_t baud, uint8_t bits,
                    uint8_t parity, uint8_t stop_bits) = nullptr;
  bool (*control)(void*, uint64_t token, bool dtr, bool rts) = nullptr;
  int32_t (*read)(void*, uint64_t token, uint8_t* dst, uint32_t capacity,
                  uint32_t timeout_ms) = nullptr;
  int32_t (*write)(void*, uint64_t token, const uint8_t* src, uint32_t length,
                   uint32_t timeout_ms) = nullptr;
  bool (*close)(void*, uint64_t token) = nullptr;
};

class ClassStreamSession {
 public:
  static constexpr uint32_t kCapacity = 4096;

  bool bound() const { return port_.open && port_.read && port_.write && port_.close; }

  bool bind(const ClassPort& port) {
    if (bound() || token_ || !port.open || !port.configure || !port.control ||
        !port.read || !port.write || !port.close) return false;
    port_ = port;
    return true;
  }

  void unbind() {
    (void)close(nullptr);
    port_ = {};
  }

  int32_t open(RuntimeStreams::Registry& registry, uint32_t owner, uint64_t device,
               uint32_t baud, uint8_t bits, uint8_t parity, uint8_t stop_bits,
               t5_stream_t* rx, t5_stream_t* tx) {
    if (rx) *rx = 0;
    if (tx) *tx = 0;
    if (!bound() || token_ || !rx || !tx || !device) return T5_STREAM_INVALID;
    const uint64_t opened = port_.open(port_.context, device);
    if (!opened) return T5_STREAM_IO;
    if (!port_.configure(port_.context, opened, baud, bits, parity, stop_bits)) {
      (void)port_.close(port_.context, opened);
      return T5_STREAM_INVALID;
    }
    t5_stream_t newRx = 0, newTx = 0;
    auto r = registry.publishEndpoint(owner, T5_STREAM_BYTES, T5_STREAM_READ,
                                      kCapacity, nullptr, 0, 0,
                                      RuntimeStreams::kStreamPublic, &newRx);
    if (r != T5_STREAM_OK) {
      (void)port_.close(port_.context, opened);
      return r;
    }
    r = registry.publishEndpoint(owner, T5_STREAM_BYTES, T5_STREAM_WRITE,
                                 kCapacity, nullptr, 0, 0,
                                 RuntimeStreams::kStreamPublic, &newTx);
    if (r != T5_STREAM_OK) {
      (void)registry.close(owner, newRx);
      (void)port_.close(port_.context, opened);
      return r;
    }
    token_ = opened;
    owner_ = owner;
    rx_ = newRx;
    tx_ = newTx;
    *rx = newRx;
    *tx = newTx;
    return T5_STREAM_OK;
  }

  int32_t grant(RuntimeStreams::Registry& registry, uint32_t consumer, uint32_t rights) {
    if (!token_) return T5_STREAM_CLOSED;
    const auto rxRights = rights & T5_STREAM_READ;
    const auto txRights = rights & T5_STREAM_WRITE;
    auto r = registry.grant(owner_, rx_, consumer, rxRights);
    if (r != T5_STREAM_OK) return r;
    return registry.grant(owner_, tx_, consumer, txRights);
  }

  int32_t control(bool dtr, bool rts) {
    if (!token_) return T5_STREAM_CLOSED;
    return port_.control(port_.context, token_, dtr, rts) ? T5_STREAM_OK : T5_STREAM_IO;
  }

  // Bind already-opened class token to published serial.port endpoints.
  int32_t attachPublished(uint32_t owner, uint64_t token, t5_stream_t rx, t5_stream_t tx) {
    if (!bound() || token_ || !token || !owner || !rx || !tx) return T5_STREAM_INVALID;
    token_ = token;
    owner_ = owner;
    rx_ = rx;
    tx_ = tx;
    return T5_STREAM_OK;
  }

  // Class I/O runs here, not under a stream-registry mutex.
  int32_t pump(RuntimeStreams::Registry& registry) {
    if (!token_) return T5_STREAM_CLOSED;
    uint8_t chunk[T5_STREAM_CHUNK];
    int32_t n = port_.read(port_.context, token_, chunk, sizeof(chunk), 1);
    if (n > 0) {
      uint32_t accepted = 0;
      const auto r = registry.produce(owner_, rx_, chunk, static_cast<uint32_t>(n), &accepted);
      if (r < 0 && r != T5_STREAM_AGAIN) return r;
    } else if (n < 0) {
      return T5_STREAM_IO;
    }
    uint32_t pending = 0;
    auto r = registry.consume(owner_, tx_, chunk, sizeof(chunk), &pending);
    if (r < 0 && r != T5_STREAM_AGAIN) return r;
    if (pending) {
      const int32_t written = port_.write(port_.context, token_, chunk, pending, 1);
      if (written < 0 || static_cast<uint32_t>(written) != pending) return T5_STREAM_IO;
    }
    return T5_STREAM_OK;
  }

  int32_t close(RuntimeStreams::Registry* registry) {
    if (!token_) return T5_STREAM_CLOSED;
    const uint64_t token = token_;
    const uint32_t owner = owner_;
    const t5_stream_t rx = rx_, tx = tx_;
    token_ = owner_ = 0;
    rx_ = tx_ = 0;
    if (registry) {
      (void)registry->close(owner, rx);
      (void)registry->close(owner, tx);
    }
    return port_.close(port_.context, token) ? T5_STREAM_OK : T5_STREAM_IO;
  }

  t5_stream_t rx() const { return rx_; }
  t5_stream_t tx() const { return tx_; }
  uint64_t token() const { return token_; }

 private:
  ClassPort port_{};
  uint64_t token_ = 0;
  uint32_t owner_ = 0;
  t5_stream_t rx_ = 0, tx_ = 0;
};

}  // namespace RuntimeUsb
