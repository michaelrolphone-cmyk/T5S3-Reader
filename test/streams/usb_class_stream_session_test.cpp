#include "runtime/usb/UsbClassStreamSession.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
void require(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL %s\n", what);
    std::exit(1);
  }
}
struct FakeClass {
  uint64_t next = 1;
  uint64_t open = 0;
  uint32_t baud = 0;
  bool dtr = false, rts = false;
  bool failClose = false;
  uint8_t rx[64]{};
  uint32_t rxLen = 0;
  uint8_t tx[64]{};
  uint32_t txLen = 0;
  uint32_t maxWrite = sizeof(tx);
  uint32_t reads = 0, writes = 0;
};
uint64_t openDev(void* ctx, uint64_t device) {
  auto* f = static_cast<FakeClass*>(ctx);
  if (!device || f->open) return 0;
  f->open = f->next++;
  return f->open;
}
bool configure(void* ctx, uint64_t token, uint32_t baud, uint8_t bits,
               uint8_t parity, uint8_t stop) {
  auto* f = static_cast<FakeClass*>(ctx);
  if (token != f->open || baud < 300 || bits < 5 || bits > 8 || parity > 4 ||
      (stop != 1 && stop != 2)) return false;
  f->baud = baud;
  return true;
}
bool control(void* ctx, uint64_t token, bool dtr, bool rts) {
  auto* f = static_cast<FakeClass*>(ctx);
  if (token != f->open) return false;
  f->dtr = dtr;
  f->rts = rts;
  return true;
}
int32_t readDev(void* ctx, uint64_t token, uint8_t* dst, uint32_t cap, uint32_t) {
  auto* f = static_cast<FakeClass*>(ctx);
  if (token != f->open || !dst || !cap) return -1;
  ++f->reads;
  const uint32_t n = f->rxLen < cap ? f->rxLen : cap;
  if (n) std::memcpy(dst, f->rx, n);
  f->rxLen = 0;
  return static_cast<int32_t>(n);
}
int32_t writeDev(void* ctx, uint64_t token, const uint8_t* src, uint32_t len, uint32_t) {
  auto* f = static_cast<FakeClass*>(ctx);
  if (token != f->open || !src || !len) return -1;
  ++f->writes;
  const uint32_t accepted = len < f->maxWrite ? len : f->maxWrite;
  if (f->txLen + accepted > sizeof(f->tx)) return -1;
  if (accepted) std::memcpy(f->tx + f->txLen, src, accepted);
  f->txLen += accepted;
  return static_cast<int32_t>(accepted);
}
bool closeDev(void* ctx, uint64_t token) {
  auto* f = static_cast<FakeClass*>(ctx);
  if (token != f->open || f->failClose) return false;
  f->open = 0;
  return true;
}
} // namespace

int main() {
  RuntimeStreams::Registry registry;
  RuntimeUsb::ClassStreamSession session;
  FakeClass fake{};
  RuntimeUsb::ClassPort port{&fake, openDev, configure, control, readDev, writeDev, closeDev};
  require(session.bind(port), "bind");
  require(!session.bind(port), "double-bind");

  t5_stream_t rx = 0, tx = 0;
  require(session.open(registry, 7, 0x11, 115200, 8, 0, 1, &rx, &tx) == T5_STREAM_OK, "open");
  require(rx && tx && rx != tx, "pair");
  require(fake.baud == 115200, "baud");
  require(session.grant(registry, 9, T5_STREAM_READ | T5_STREAM_WRITE) == T5_STREAM_OK, "grant");
  require(session.grant(registry, 10, T5_STREAM_READ) == T5_STREAM_OK, "read-only-grant");
  require(session.grant(registry, 11, T5_STREAM_WRITE) == T5_STREAM_OK, "write-only-grant");
  uint32_t n = 0;
  char got[32]{};
  require(registry.write(10, tx, "x", 1, &n) == T5_STREAM_INVALID, "read-only-is-not-tx");
  require(registry.read(11, rx, got, 1, &n) == T5_STREAM_INVALID, "write-only-is-not-rx");

  const char inbound[] = "from-device";
  std::memcpy(fake.rx, inbound, sizeof(inbound) - 1);
  fake.rxLen = sizeof(inbound) - 1;
  require(session.pump(registry) == T5_STREAM_OK, "pump-rx");
  require(registry.read(9, rx, got, sizeof(got), &n) == T5_STREAM_OK, "consumer-read");
  require(n == sizeof(inbound) - 1, "rx-len");
  require(std::memcmp(got, inbound, n) == 0, "rx-bytes");

  const char outbound[] = "to-device";
  require(registry.write(9, tx, outbound, sizeof(outbound) - 1, &n) == T5_STREAM_OK, "consumer-write");
  require(session.pump(registry) == T5_STREAM_OK, "pump-tx");
  require(fake.writes == 1, "class-write");
  require(fake.txLen == sizeof(outbound) - 1, "tx-len");
  require(std::memcmp(fake.tx, outbound, fake.txLen) == 0, "tx-bytes");

  // The class can accept a prefix or zero bytes; neither may lose or
  // duplicate any previously consumed stream bytes.
  fake.txLen = 0;
  const char partial[] = "partial-transfer";
  fake.maxWrite = 3;
  require(registry.write(9, tx, partial, sizeof(partial) - 1, &n) == T5_STREAM_OK,
          "queue-partial");
  require(session.pump(registry) == T5_STREAM_OK && fake.txLen == 3,
          "first-short-write");
  fake.maxWrite = 0;
  require(session.pump(registry) == T5_STREAM_OK && fake.txLen == 3,
          "zero-write-retains-pending");
  fake.maxWrite = 3;
  for (unsigned attempt = 0; attempt < 6; ++attempt)
    require(session.pump(registry) == T5_STREAM_OK, "resume-short-write");
  require(fake.txLen == sizeof(partial) - 1 &&
          std::memcmp(fake.tx, partial, fake.txLen) == 0, "short-writes-ordered");

  // A full RX endpoint must stop class reads and preserve the pending chunk.
  uint8_t padding[T5_STREAM_CHUNK]{};
  std::memset(padding, 'P', sizeof(padding));
  for (uint32_t i = 0; i < session.kCapacity / sizeof(padding); ++i)
    require(registry.produce(7, rx, padding, sizeof(padding), &n) == T5_STREAM_OK &&
            n == sizeof(padding), "fill-rx");
  std::memcpy(fake.rx, inbound, sizeof(inbound) - 1);
  fake.rxLen = sizeof(inbound) - 1;
  const uint32_t readsBefore = fake.reads;
  require(session.pump(registry) == T5_STREAM_OK && fake.reads == readsBefore + 1,
          "class-read-under-pressure");
  require(session.pump(registry) == T5_STREAM_OK && fake.reads == readsBefore + 1,
          "no-read-ahead-while-full");
  uint8_t drained[T5_STREAM_CHUNK]{};
  require(registry.read(9, rx, drained, sizeof(drained), &n) == T5_STREAM_OK &&
          n == sizeof(drained), "make-rx-room");
  require(session.pump(registry) == T5_STREAM_OK && fake.reads == readsBefore + 1,
          "replay-held-rx-before-next-class-read");
  for (uint32_t i = 0; i < session.kCapacity / sizeof(padding) - 1; ++i) {
    require(registry.read(9, rx, drained, sizeof(drained), &n) == T5_STREAM_OK &&
            n == sizeof(drained), "drain-padding");
    for (uint32_t j = 0; j < n; ++j) require(drained[j] == 'P', "padding-order");
  }
  require(registry.read(9, rx, got, sizeof(got), &n) == T5_STREAM_OK &&
          n == sizeof(inbound) - 1 && std::memcmp(got, inbound, n) == 0,
          "held-rx-delivered-exactly-once");
  require(session.control(true, false) == T5_STREAM_OK && fake.dtr && !fake.rts, "lines");

  fake.failClose = true;
  require(session.close(&registry) == T5_STREAM_IO, "failed-close-reported");
  require(session.token() == fake.open && session.bound(), "failed-close-pins-elf");
  require(!session.unbind(), "failed-close-refuses-unbind");
  require(registry.read(9, rx, got, 1, &n) == T5_STREAM_AGAIN,
          "failed-close-preserves-endpoints");
  fake.failClose = false;
  require(session.close(&registry) == T5_STREAM_OK, "retry-close");
  require(!fake.open, "class-closed");
  require(registry.read(9, rx, got, 1, &n) == T5_STREAM_INVALID, "revoked");
  require(session.unbind() && !session.bound(), "unbind-after-close");
  std::puts("ok");
  return 0;
}
