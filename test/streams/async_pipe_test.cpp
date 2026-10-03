#include "runtime/streams/StreamRuntime.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace RuntimeStreams;
namespace {
constexpr uint32_t owner = 17;
struct Adapter { unsigned reads = 0, closes = 0; };
int32_t read(void* context, void* data, uint32_t, uint32_t* count) {
  auto& a = *static_cast<Adapter*>(context);
  assert(!a.closes); ++a.reads;
  std::memcpy(data, "abc", 3); *count = 3;
  return T5_STREAM_EOF;
}
void close(void* context) { ++static_cast<Adapter*>(context)->closes; }
int32_t write(void*, const void*, uint32_t, uint32_t*) { return T5_STREAM_AGAIN; }
t5_pipe_info_t info(Registry& r, t5_pipe_t p) {
  t5_pipe_info_t value{}; value.struct_size = sizeof(value);
  assert(r.pipeInfo(owner, p, &value) == T5_STREAM_OK); return value;
}
struct Path { t5_stream_t source = 0, dest = 0; t5_pipe_t pipe = 0; };
Path path(Registry& r, Adapter& a) {
  Path p;
  assert(r.attach(owner, T5_STREAM_BYTES, T5_STREAM_READ,
                  {&a, read, nullptr, nullptr, nullptr, close}, &p.source) == T5_STREAM_OK);
  assert(r.buffer(owner, 16, &p.dest) == T5_STREAM_OK);
  assert(r.connect(owner, p.source, p.dest, 0, &p.pipe) == T5_STREAM_OK);
  return p;
}
void pauseAndEof() {
  Registry r; Adapter a; const auto p = path(r, a);
  ExternalIo io{}, duplicate{};
  assert(r.pumpPrepare(&io));
  assert(!r.pumpPrepare(&duplicate)); // no concurrent call into same adapter
  assert(r.pause(owner, p.pipe, true) == T5_STREAM_OK);
  char bytes[16]{}; uint32_t count = 0;
  const auto result = io.provider.read(io.provider.context, bytes, io.request, &count);
  r.pumpComplete(io, result, bytes, count);
  r.pumpComplete(io, result, bytes, count); // same completion cannot replay data
  assert(info(r, p.pipe).state == T5_PIPE_PAUSED && info(r, p.pipe).buffered == 3);
  assert(!r.pumpPrepare(&duplicate));
  assert(r.pause(owner, p.pipe, false) == T5_STREAM_OK);
  for (int i = 0; i < 3; ++i) assert(!r.pumpPrepare(&duplicate));
  assert(info(r, p.pipe).state == T5_PIPE_DONE && a.reads == 1);
  assert(r.read(owner, p.dest, bytes, sizeof(bytes), &count) == T5_STREAM_OK && count == 3);
  assert(!std::memcmp(bytes, "abc", 3));
  r.release(owner); assert(a.closes == 1);
}
void deferredClose(bool ownerExit) {
  Registry r; Adapter a; const auto p = path(r, a);
  ExternalIo io{}; assert(r.pumpPrepare(&io));
  if (ownerExit) r.release(owner);
  else assert(r.close(owner, p.source) == T5_STREAM_BUSY);
  assert(a.closes == 0);
  char bytes[16]{}; uint32_t count = 0;
  const auto result = io.provider.read(io.provider.context, bytes, io.request, &count);
  r.pumpComplete(io, result, bytes, count);
  assert(a.closes == 1);
  t5_stream_t replacement = 0;
  assert(r.buffer(owner, 8, &replacement) == T5_STREAM_OK && replacement != p.source);
  r.pumpComplete(io, result, bytes, count);
  assert(r.read(owner, replacement, bytes, sizeof(bytes), &count) == T5_STREAM_AGAIN);
  assert(a.closes == 1); r.release(owner);
}
void cancelledPipeReuse() {
  Registry r; Adapter a; const auto old = path(r, a);
  ExternalIo io{}; assert(r.pumpPrepare(&io));
  assert(r.cancel(owner, old.pipe) == T5_STREAM_OK);
  assert(r.closePipe(owner, old.pipe) == T5_STREAM_OK);
  // A replacement pipe cannot race the unfinished adapter, but may use other
  // endpoints immediately. Late data must never reach that replacement.
  t5_pipe_t replacement = 0;
  assert(r.connect(owner, old.source, old.dest, 0, &replacement) == T5_STREAM_BUSY);
  t5_stream_t other = 0;
  assert(r.buffer(owner, 8, &other) == T5_STREAM_OK);
  assert(r.connect(owner, other, old.dest, 0, &replacement) == T5_STREAM_OK);
  assert(replacement != old.pipe);
  r.pumpComplete(io, T5_STREAM_OK, "stale", 5);
  r.pump();
  assert(info(r, replacement).bytes_transferred == 0);
  assert(info(r, replacement).state == T5_PIPE_RUNNING);
  assert(r.close(owner, old.source) == T5_STREAM_OK && a.closes == 1);
  r.release(owner);
}
void fairness() {
  Registry r; Adapter a, b; auto first = path(r, a), second = path(r, b);
  for (unsigned turn = 0; turn < 12; ++turn) {
    ExternalIo io{}; assert(r.pumpPrepare(&io));
    assert(io.streamIndex == ((turn % 2 ? second.source : first.source) & 255) - 1);
    r.pumpComplete(io, T5_STREAM_AGAIN, nullptr, 0);
  }
  assert(info(r, first.pipe).stalls == 6 && info(r, second.pipe).stalls == 6);
  r.release(owner);
}
void partialWritePause() {
  Registry r; t5_stream_t source = 0, dest = 0; t5_pipe_t pipe = 0; uint32_t n = 0;
  assert(r.buffer(owner, 8, &source) == T5_STREAM_OK);
  assert(r.attach(owner, T5_STREAM_BYTES, T5_STREAM_WRITE,
                  {nullptr, nullptr, write, nullptr, nullptr, nullptr}, &dest) == T5_STREAM_OK);
  assert(r.write(owner, source, "abcdef", 6, &n) == T5_STREAM_OK);
  assert(r.finish(owner, source) == T5_STREAM_OK);
  assert(r.connect(owner, source, dest, 0, &pipe) == T5_STREAM_OK);
  ExternalIo io{}; assert(r.pumpPrepare(&io) && !io.reading && io.request == 6);
  assert(r.pause(owner, pipe, true) == T5_STREAM_OK);
  r.pumpComplete(io, T5_STREAM_OK, nullptr, 2);
  assert(info(r, pipe).buffered == 4 && info(r, pipe).bytes_transferred == 2);
  assert(r.pause(owner, pipe, false) == T5_STREAM_OK);
  ExternalIo next{}; assert(r.pumpPrepare(&next) && next.request == 4);
  assert(!std::memcmp(next.payload.data(), "cdef", 4));
  r.pumpComplete(io, T5_STREAM_OK, nullptr, 2); // stale ticket cannot unpin next
  assert(r.close(owner, dest) == T5_STREAM_BUSY);
  r.pumpComplete(next, T5_STREAM_OK, nullptr, 4);
  assert(r.close(owner, dest) == T5_STREAM_INVALID);
  r.release(owner);
}
}
int main() {
  pauseAndEof(); deferredClose(false); deferredClose(true); cancelledPipeReuse(); fairness(); partialWritePause();
  std::cout << "Asynchronous pipe lifecycle tests passed\n";
}
