#include "runtime/streams/StreamRuntime.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
using namespace RuntimeStreams;
constexpr uint32_t owner = 17;
t5_stream_t buffer(Registry& r, unsigned size) { t5_stream_t h; assert(r.buffer(owner, size, &h) == 0); return h; }
t5_pipe_info_t info(Registry& r, t5_pipe_t p) { t5_pipe_info_t i{}; i.struct_size = sizeof(i); assert(r.pipeInfo(owner, p, &i) == 0); return i; }
struct Source { unsigned pos = 0; bool closed = false; };
int32_t sourceRead(void* ctx, void* out, uint32_t size, uint32_t* count) {
  auto& s = *static_cast<Source*>(ctx);
  if (s.pos == 10000) return T5_STREAM_EOF;
  *count = std::min(size, 10000 - s.pos);
  for (unsigned i = 0; i < *count; ++i) static_cast<uint8_t*>(out)[i] = (s.pos + i) % 251;
  s.pos += *count; return 0;
}
int32_t sourceSeek(void* ctx, uint64_t offset) {
  if (offset > 10000) return T5_STREAM_INVALID;
  static_cast<Source*>(ctx)->pos = offset; return 0;
}
void sourceClose(void* ctx) { static_cast<Source*>(ctx)->closed = true; }
int32_t failWrite(void*, const void*, uint32_t, uint32_t*) { return T5_STREAM_DISCONNECTED; }
int main() {
  Registry r;
  uint32_t count = 999;
  uint8_t bytes[512]{};
  auto a = buffer(r, 7), b = buffer(r, 3);
  assert(r.read(owner, a, bytes, sizeof(bytes), &count) == T5_STREAM_AGAIN && count == 0);
  assert(r.write(owner + 1, a, "abc", 3, &count) == T5_STREAM_INVALID);
  assert(r.write(owner, a, "123456789", 9, &count) == 0 && count == 7);
  assert(r.write(owner, a, "x", 1, &count) == T5_STREAM_AGAIN && count == 0);
  assert(r.seek(owner, a, 0) == T5_STREAM_UNSUPPORTED);
  t5_pipe_t p;
  assert(r.connect(owner, a, b, 1, &p) == T5_STREAM_UNSUPPORTED);
  assert(r.connect(owner, a, b, 0, &p) == 0);
  t5_pipe_t duplicate;
  assert(r.connect(owner, b, a, 0, &duplicate) == T5_STREAM_INVALID);
  assert(r.read(owner, a, bytes, sizeof(bytes), &count) == T5_STREAM_BUSY);
  assert(r.write(owner, b, "x", 1, &count) == T5_STREAM_BUSY);
  assert(r.finish(owner, a) == 0);
  r.pump();
  auto stats = info(r, p);
  assert(stats.bytes_transferred == 3 && stats.buffered == 4);
  for (int i = 0; i < 100; ++i) r.pump();
  stats = info(r, p); assert(stats.stalls == 100 && stats.buffered == 4);
  assert(r.pause(owner, p, true) == 0);
  assert(r.read(owner, b, bytes, 3, &count) == 0 && count == 3 && !memcmp(bytes, "123", 3));
  r.pump(); assert(info(r, p).bytes_transferred == 3);
  assert(r.pause(owner, p, false) == 0); r.pump();
  assert(r.read(owner, b, bytes, 3, &count) == 0 && !memcmp(bytes, "456", 3));
  r.pump(); r.pump(); assert(info(r, p).state == T5_PIPE_DONE);
  assert(r.read(owner, b, bytes, 3, &count) == 0 && count == 1 && bytes[0] == '7');
  assert(r.closePipe(owner, p) == 0);
  assert(r.close(owner, a) == 0);
  auto newer = buffer(r, 8); assert(newer != a);
  assert(r.read(owner, a, bytes, 1, &count) == T5_STREAM_INVALID);
  r.release(owner);

  // Ten thousand ordered bytes through a three-byte consumer, with bounded
  // staging and no whole-source allocation. Provider EOF and seek are explicit.
  Source source;
  t5_stream_t s;
  Provider provider{&source, sourceRead, nullptr, sourceSeek, nullptr, sourceClose};
  assert(r.attach(owner, T5_STREAM_RECORDS, T5_STREAM_READ, provider, &s) == T5_STREAM_UNSUPPORTED);
  assert(r.attach(owner, T5_STREAM_BYTES, T5_STREAM_READ | T5_STREAM_SEEK, provider, &s) == 0);
  b = buffer(r, 3); assert(r.connect(owner, s, b, 0, &p) == 0);
  unsigned received = 0;
  for (int turn = 0; turn < 10000 && info(r, p).state == T5_PIPE_RUNNING; ++turn) {
    r.pump();
    assert(r.read(owner, b, bytes, sizeof(bytes), &count) >= 0);
    for (unsigned i = 0; i < count; ++i) assert(bytes[i] == (received + i) % 251);
    received += count;
    assert(info(r, p).buffered <= T5_STREAM_CHUNK);
  }
  assert(received == 10000 && info(r, p).state == T5_PIPE_DONE);
  assert(r.seek(owner, s, 10001) == T5_STREAM_INVALID);
  assert(r.seek(owner, s, 9999) == 0);
  assert(r.read(owner, s, bytes, 512, &count) == 0 && count == 1);
  assert(r.read(owner, s, bytes, 512, &count) == T5_STREAM_EOF);
  r.release(owner); assert(source.closed);

  // Firmware-fed read-only HTTP-style buffer, error after buffered data.
  assert(r.buffer(owner, 4, &a, T5_STREAM_READ) == 0);
  assert(r.write(owner, a, "a", 1, &count) == T5_STREAM_DENIED);
  assert(r.produce(owner, a, "abcd", 4, &count) == 0 && count == 4);
  assert(r.finish(owner, a, T5_STREAM_TIMEOUT) == 0);
  assert(r.read(owner, a, bytes, 4, &count) == 0 && count == 4);
  assert(r.read(owner, a, bytes, 4, &count) == T5_STREAM_TIMEOUT);
  r.release(owner);

  // Destination removal, cancellation, stale pipe and endpoint handles.
  a = buffer(r, 8); b = buffer(r, 8);
  assert(r.connect(owner, a, b, 0, &p) == 0);
  assert(r.close(owner, b) == 0 && info(r, p).last_error == T5_STREAM_CLOSED);
  auto oldPipe = p; assert(r.closePipe(owner, p) == 0);
  b = buffer(r, 8); assert(r.connect(owner, a, b, 0, &p) == 0 && p != oldPipe);
  assert(r.pause(owner, oldPipe, true) == T5_STREAM_INVALID);
  assert(r.cancel(owner, p) == 0 && info(r, p).state == T5_PIPE_CANCELLED);
  assert(r.write(owner, b, "q", 1, &count) == 0);
  r.release(owner);
  assert(r.read(owner, b, bytes, 1, &count) == T5_STREAM_INVALID);

  // Failures propagate; all slots recover after owner exit.
  a = buffer(r, 4);
  Provider failing{}; failing.write = failWrite;
  assert(r.attach(owner, T5_STREAM_BYTES, T5_STREAM_WRITE, failing, &b) == 0);
  assert(r.write(owner, a, "abc", 3, &count) == 0);
  assert(r.connect(owner, a, b, 0, &p) == 0); r.pump();
  assert(info(r, p).state == T5_PIPE_FAILED && info(r, p).last_error == T5_STREAM_DISCONNECTED);
  r.release(owner);
  for (unsigned i = 0; i < Registry::MaxStreams; ++i) buffer(r, 1);
  assert(r.buffer(owner, 1, &a) == T5_STREAM_LIMIT && a == 0);
  r.release(owner);

  // Both pipes advance each scheduler turn, including independent RX/TX paths.
  auto rx = buffer(r, 16), tx = buffer(r, 16), terminal = buffer(r, 16), device = buffer(r, 16);
  t5_pipe_t p2;
  assert(r.connect(owner, rx, terminal, 0, &p) == 0);
  assert(r.connect(owner, tx, device, 0, &p2) == 0);
  assert(r.write(owner, rx, "in", 2, &count) == 0);
  assert(r.write(owner, tx, "out", 3, &count) == 0);
  r.pump(); assert(info(r, p).bytes_transferred == 2 && info(r, p2).bytes_transferred == 3);
  r.release(owner);
  std::cout << "Stream runtime tests passed\n";
}
