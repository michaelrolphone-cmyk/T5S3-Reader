#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "network/HttpClientBudget.h"

static uint32_t nowMs;
static unsigned refusedYields;
static void cooperate() { ++refusedYields; }
static uint32_t clockMs() { return nowMs; }
static void delay(unsigned) { nowMs += 100; }
class FakeClient {
 public:
  enum Mode { Idle, Ready, Drip } mode = Idle;
  unsigned stops = 0, connects = 0, reads = 0, delivered = 0, readyBytes = 64;
  uint32_t born = nowMs, next = nowMs + 4000, connectDelay = 0;
  bool live = true;
  virtual ~FakeClient() = default;
  virtual int connect(const char*, uint16_t, int32_t) {
    ++connects;
    nowMs += connectDelay;
    live = true;
    return 1;
  }
  virtual int available() {
    return live ? (mode == Ready ? readyBytes - delivered : mode == Drip && nowMs >= next ? 1 : 0) : 0;
  }
  virtual uint8_t connected() { return live && nowMs - born < 610000; }
  virtual int peek() { return available() ? 0x42 : -1; }
  virtual int read() {
    if (!available()) return -1;
    ++reads;
    ++delivered;
    next = nowMs + 4000;
    return 0x42;
  }
  virtual int read(uint8_t* p, size_t n) {
    size_t done = 0;
    while (done < n) {
      const int value = FakeClient::read();
      if (value < 0) break;
      p[done++] = value;
    }
    return static_cast<int>(done);
  }
  virtual size_t readBytes(char* p, size_t n) { return static_cast<size_t>(read(reinterpret_cast<uint8_t*>(p), n)); }
  virtual size_t readBytes(uint8_t* p, size_t n) { return readBytes(reinterpret_cast<char*>(p), n); }
  virtual size_t write(uint8_t byte) { return write(&byte, 1); }
  virtual size_t write(const uint8_t*, size_t n) { return n; }
  virtual void stop() {
    ++stops;
    live = false;
  }
};
class Stream {
 public:
  size_t count = 0;
  size_t write(const uint8_t*, size_t n) {
    count += n;
    return n;
  }
  int getWriteError() const { return 0; }
  void clearWriteError() {}
};
#ifdef U1_REAL_HTTP_BODY
#define HTTP_TCP_BUFFER_SIZE 1460
#define HTTPC_ERROR_STREAM_WRITE (-10)
#define HTTPC_ERROR_TOO_LESS_RAM (-8)
#define log_d(...) ((void)0)
#define log_w(...) ((void)0)
class HTTPClient {
 public:
  FakeClient* _client;
  explicit HTTPClient(FakeClient& client) : _client(&client) {}
  bool connected() { return _client->connected() || _client->available(); }
  int writeToStreamDataBlock(Stream*, int);
};
#include "upstream_body.inc"
#endif
int main() {
  using HttpClientBudget::Budget;
  using Client = HttpClientBudget::Client<FakeClient>;
  nowMs = 0;
  Budget idle(clockMs, 300000, 5000, cooperate);
  Client client(idle);
  FakeClient* dispatched = &client;
  nowMs = 4999;
  assert(dispatched->connected());
  nowMs = 5000;
  assert(!dispatched->connected() && client.stops == 1 && idle.failure() == Budget::Failure::Idle);
  assert(refusedYields > 0);
  uint8_t byte{};
  assert(dispatched->available() == 0 && dispatched->read() == -1 && dispatched->read(&byte, 1) == -1);
  assert(dispatched->peek() == -1 && dispatched->write(&byte, 1) == 0 && dispatched->write(byte) == 0);
  assert(dispatched->connect("host", 80, 5000) == 0 && client.connects == 0 && client.stops == 1);
  nowMs = 0;
  Budget total(clockMs, 10000, 5000);
  Client progressing(total);
  for (unsigned i = 0; i < 9; ++i) {
    nowMs += 1000;
    assert(progressing.write(byte) == 1);
  }
  nowMs = 10000;
  assert(progressing.write(byte) == 0 && total.failure() == Budget::Failure::Deadline);
  nowMs = ~0u - 2000;
  Budget wrap(clockMs, 300000, 5000);
  Client wrapped(wrap);
  nowMs += 4999;
  assert(wrapped.connected());
  ++nowMs;
  assert(!wrapped.connected());
  nowMs = 0;
  Budget connectBudget(clockMs, 20000);
  Client connecting(connectBudget);
  connecting.connectDelay = 10000;
  assert(connecting.connect("host", 443, 5000) == 1);  // separate connect/TLS bound
  nowMs = 14000;
  assert(connecting.write(byte) == 1);
  connecting.stop();
  assert(connecting.connect("redirect", 443, 5000) == 0);
  assert(connectBudget.failure() == Budget::Failure::Deadline);  // redirect never refreshes total
  nowMs = 0;
  Budget readBudget(clockMs, 300000, 5000);
  Client reader(readBudget);
  reader.mode = FakeClient::Ready;
  nowMs = 4000;
  assert(reader.read(&byte, 1) == 1);
  nowMs = 8000;
  assert(reader.read() == 0x42);
  assert(readBudget.failure() == Budget::Failure::None);
  nowMs = 0;
  Budget headerBudget(clockMs);
  Client header(headerBudget);
  header.mode = FakeClient::Ready;
  header.readyBytes = 20000;
  for (unsigned i = 0; i < 8192; ++i) assert(header.read() == 0x42);
  assert(header.read() == -1 && headerBudget.failure() == Budget::Failure::HeaderLine && header.stops == 1);
  nowMs = 0;
  Budget binaryBudget(clockMs);
  Client binary(binaryBudget);
  binary.mode = FakeClient::Ready;
  binary.readyBytes = 20000;
  uint8_t block[1000];
  for (unsigned i = 0; i < 20; ++i) assert(binary.readBytes(block, sizeof(block)) == sizeof(block));
  assert(binaryBudget.failure() == Budget::Failure::None);  // binary blocks have no newline requirement
#ifdef U1_REAL_HTTP_BODY
  nowMs = 0;
  FakeClient baseline;
  HTTPClient old(baseline);
  Stream discarded;
  assert(old.writeToStreamDataBlock(&discarded, 1) < 0 && nowMs >= 610000);
  // The unchanged upstream loop passes its requested deadline only with the
  // production adapter. The fake peer eventually disconnects for the baseline.
  nowMs = 0;
  Budget bounded(clockMs);
  Client fixed(bounded);
  HTTPClient http(fixed);
  Stream out;
  assert(http.writeToStreamDataBlock(&out, 1) < 0 && nowMs == 30000 && fixed.stops == 1);
  nowMs = 0;
  Budget dripBudget(clockMs);
  Client drip(dripBudget);
  drip.mode = FakeClient::Drip;
  HTTPClient slow(drip);
  Stream unknown;
  assert(slow.writeToStreamDataBlock(&unknown, -1) >= 0 && nowMs == 300000);
  assert(dripBudget.failure() == Budget::Failure::Deadline);  // caller must reject nonnegative partial EOF
  nowMs = 0;
  Budget goodBudget(clockMs);
  Client good(goodBudget);
  good.mode = FakeClient::Ready;
  good.readyBytes = 20000;
  HTTPClient complete(good);
  Stream sink;
  assert(complete.writeToStreamDataBlock(&sink, 20000) == 20000 && sink.count == 20000);
  assert(goodBudget.failure() == Budget::Failure::None);
  puts(
      "Actual Arduino HTTP body loop: baseline exceeds budget; adapter terminates idle/drip and preserves complete "
      "bodies PASS");
#else
  (void)delay;  // Same adapter fixture also supports the dependency-backed loop.
#endif
  puts(
      "HTTP client budget: dynamic dispatch, idle/total/wrap, redirect persistence, progress and same-worker stop "
      "PASS");
}
