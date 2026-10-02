#include "runtime/streams/StreamRuntime.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace RuntimeStreams;
namespace {
constexpr uint32_t owner = 7, consumer = 9;
struct Adapter { unsigned closes = 0; };
int32_t read(void*, void*, uint32_t, uint32_t*) { return T5_STREAM_AGAIN; }
int32_t write(void*, const void*, uint32_t, uint32_t*) { return T5_STREAM_AGAIN; }
int32_t finish(void*) { return T5_STREAM_AGAIN; }
int32_t seek(void*, uint64_t) { return T5_STREAM_OK; }
void close(void* p) { ++static_cast<Adapter*>(p)->closes; }
t5_stream_t attach(Registry& r, Adapter& a) {
  t5_stream_t h = 0;
  assert(r.attach(owner, T5_STREAM_BYTES, 7,
                   {&a, read, write, seek, finish, close}, &h) == T5_STREAM_OK);
  return h;
}
t5_stream_info_t info(Registry& r, t5_stream_t h) {
  t5_stream_info_t i{}; i.struct_size = sizeof(i);
  assert(r.info(owner, h, &i) == T5_STREAM_OK); return i;
}
void revokeAndRegrant() {
  Registry r; Adapter a; auto h = attach(r, a); DirectIo io{};
  char output[8] = "xxxxxxx"; uint32_t count = 9;
  assert(r.grant(owner, h, consumer, T5_STREAM_READ) == T5_STREAM_OK);
  assert(r.read(consumer, h, output, sizeof(output), &count, &io) == T5_STREAM_OK);
  assert(io.transfer.pending && !count);
  std::memcpy(io.transfer.payload.data(), "private", 7);
  assert(r.revoke(owner, h, consumer) == T5_STREAM_OK);
  assert(r.grant(owner, h, consumer, T5_STREAM_READ) == T5_STREAM_OK);
  assert(r.directComplete(io, T5_STREAM_OK, 7, output, &count) == T5_STREAM_DENIED && !count);
  assert(!std::memcmp(output, "xxxxxxx", 7));
  DirectIo next{};
  assert(r.read(consumer, h, output, sizeof(output), &count, &next) == T5_STREAM_OK);
  assert(r.directComplete(io, T5_STREAM_OK, 7, output, &count) == T5_STREAM_CLOSED);
  assert(r.seek(owner, h, 0) == T5_STREAM_BUSY); // stale completion did not unpin next
  assert(r.directComplete(next, T5_STREAM_AGAIN, 0, output, &count) == T5_STREAM_AGAIN);
  r.release(owner); assert(a.closes == 1);
}
void controlsAndPartialWrites() {
  Registry r; Adapter a; auto h = attach(r, a); DirectIo io{};
  char input[] = "bytes"; uint32_t count = 0;
  assert(r.write(owner, h, input, 5, &count, &io) == T5_STREAM_OK && io.transfer.pending);
  input[0] = 'x'; assert(!std::memcmp(io.transfer.payload.data(), "bytes", 5));
  assert(r.directComplete(io, T5_STREAM_AGAIN, 2, nullptr, &count) == T5_STREAM_AGAIN && count == 2);
  assert(info(r, h).bytes_written == 2);
  assert(r.finish(owner, h, T5_STREAM_EOF, &io) == T5_STREAM_OK && io.transfer.pending);
  assert(r.directComplete(io, T5_STREAM_AGAIN, 0, nullptr, nullptr) == T5_STREAM_AGAIN);
  assert(info(r, h).terminal == 0);
  assert(r.finish(owner, h, T5_STREAM_EOF, &io) == T5_STREAM_OK && io.transfer.pending);
  assert(r.directComplete(io, T5_STREAM_OK, 0, nullptr, nullptr) == T5_STREAM_OK);
  assert(info(r, h).terminal == T5_STREAM_EOF);
  assert(r.seek(owner, h, 10, &io) == T5_STREAM_OK && io.offset == 10);
  assert(r.directComplete(io, T5_STREAM_IO, 0, nullptr, nullptr) == T5_STREAM_IO);
  assert(info(r, h).terminal == T5_STREAM_EOF); // failed seek does not reopen stream
  assert(r.seek(owner, h, 0, &io) == T5_STREAM_OK);
  assert(r.directComplete(io, T5_STREAM_OK, 0, nullptr, nullptr) == T5_STREAM_OK);
  assert(info(r, h).terminal == 0);
  assert(r.read(owner, h, input, 1, &count, &io) == T5_STREAM_OK);
  assert(r.directComplete(io, T5_STREAM_OK, 2, input, &count) == T5_STREAM_IO && count == 0);
  assert(info(r, h).terminal == T5_STREAM_IO);
  r.release(owner);
}
void retireOutsideLock() {
  Registry r; Adapter a, b; auto h = attach(r, a); auto other = attach(r, b);
  DirectIo io{}; char output[4]{}; uint32_t count = 0;
  assert(r.read(owner, h, output, sizeof(output), &count, &io) == T5_STREAM_OK);
  std::array<Provider, Registry::MaxStreams> retired{};
  r.release(owner, &retired);
  assert(a.closes == 0 && b.closes == 0);
  unsigned retiredCount = 0;
  for (auto& p : retired) if (p.close) { ++retiredCount; p.close(p.context); }
  assert(retiredCount == 1 && b.closes == 1 && a.closes == 0);
  Provider last{};
  assert(r.directComplete(io, T5_STREAM_OK, 1, output, &count, &last) == T5_STREAM_CLOSED);
  assert(count == 0 && a.closes == 0 && last.close);
  last.close(last.context); assert(a.closes == 1);
  assert(r.close(owner, h) == T5_STREAM_INVALID && r.close(owner, other) == T5_STREAM_INVALID);
  Provider duplicate{};
  assert(r.directComplete(io, T5_STREAM_OK, 1, output, &count, &duplicate) == T5_STREAM_CLOSED);
  assert(!duplicate.close);
}
}
int main() {
  revokeAndRegrant(); controlsAndPartialWrites(); retireOutsideLock();
  std::cout << "Direct stream grants, partial I/O, control retry and retirement tests passed\n";
}
