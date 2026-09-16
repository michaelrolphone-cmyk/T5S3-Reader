#include "runtime/streams/StreamRuntime.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace RuntimeStreams;

namespace {
constexpr uint32_t owner = 42;
t5_stream_t records(Registry& r, const char* schema, uint32_t maxSize = 16, uint32_t count = 2) {
  t5_stream_t h = 0;
  assert(r.recordBuffer(owner, schema, maxSize, count, &h) == T5_STREAM_OK && h);
  return h;
}
t5_pipe_info_t pipeInfo(Registry& r, t5_pipe_t p) {
  t5_pipe_info_t info{};
  info.struct_size = sizeof(info);
  assert(r.pipeInfo(owner, p, &info) == T5_STREAM_OK);
  return info;
}
}
int main() {
  Registry r;
  t5_stream_t wrong = 111;
  assert(r.recordBuffer(owner, "location.fix", 16, 2, &wrong) == T5_STREAM_INVALID && !wrong);
  assert(r.recordBuffer(owner, "location.fix.v1", 16, 2, &wrong, T5_STREAM_SEEK) == T5_STREAM_INVALID);
  auto src = records(r, "location.fix.v1");
  auto dst = records(r, "location.fix.v1", 16, 1);
  auto nextVersion = records(r, "location.fix.v2");
  auto unrelated = records(r, "sensor.measurement.v1");
  t5_stream_t bytes = 0;
  assert(r.buffer(owner, 16, &bytes) == T5_STREAM_OK);

  t5_pipe_t p = 123;
  assert(r.connect(owner, src, nextVersion, T5_PIPE_BLOCK_PRODUCER, &p) == T5_STREAM_UNSUPPORTED && !p);
  assert(r.connect(owner, src, unrelated, T5_PIPE_BLOCK_PRODUCER, &p) == T5_STREAM_UNSUPPORTED && !p);
  assert(r.connect(owner, src, bytes, T5_PIPE_BLOCK_PRODUCER, &p) == T5_STREAM_UNSUPPORTED && !p);
  assert(r.connect(owner, bytes, dst, T5_PIPE_BLOCK_PRODUCER, &p) == T5_STREAM_UNSUPPORTED && !p);
  assert(r.connect(owner, src, dst, T5_PIPE_BLOCK_PRODUCER, &p) == T5_STREAM_OK);

  char schema[RecordQueue::MaxSchema]{};
  RecordQueue::Stats stats{};
  assert(r.recordInfo(owner, src, schema, 2, &stats) == T5_STREAM_LIMIT);
  assert(r.recordInfo(owner, src, schema, sizeof(schema), &stats) == T5_STREAM_OK);
  assert(!std::strcmp(schema, "location.fix.v1") && stats.capacity_records == 2);
  t5_stream_info_t si{};
  si.struct_size = sizeof(si);
  assert(r.info(owner, src, &si) == T5_STREAM_OK && si.kind == T5_STREAM_RECORDS);
  uint32_t n = 17;
  char data[512]{};
  assert(r.read(owner, src, data, sizeof(data), &n) == T5_STREAM_UNSUPPORTED && !n);
  assert(r.write(owner, src, "abc", 3, &n) == T5_STREAM_UNSUPPORTED && !n);
  assert(r.produce(owner, src, "abc", 3, &n) == T5_STREAM_UNSUPPORTED && !n);
  assert(r.readRecord(owner, src, data, sizeof(data), &n) == T5_STREAM_BUSY && !n);
  assert(r.writeRecord(owner, dst, "x", 1) == T5_STREAM_BUSY);
  assert(r.writeRecord(owner + 1, src, "x", 1) == T5_STREAM_INVALID);
  assert(r.writeRecord(owner, src, "A", 1) == T5_STREAM_OK);
  assert(r.writeRecord(owner, src, "BB", 2) == T5_STREAM_OK);
  assert(r.writeRecord(owner, src, "CCC", 3) == T5_STREAM_AGAIN);
  assert(r.finish(owner, src) == T5_STREAM_OK);
  r.pump();
  assert(pipeInfo(r, p).bytes_transferred == 1);
  r.pump();
  assert(pipeInfo(r, p).buffered == 2 && pipeInfo(r, p).stalls >= 1);
  assert(r.readRecord(owner, dst, data, 0, &n) == T5_STREAM_LIMIT && n == 0);
  assert(r.readRecord(owner, dst, data, sizeof(data), &n) == T5_STREAM_OK && n == 1 && data[0] == 'A');
  r.pump();
  assert(pipeInfo(r, p).bytes_transferred == 3);
  r.pump();
  assert(pipeInfo(r, p).state == T5_PIPE_DONE);
  assert(r.readRecord(owner, dst, data, sizeof(data), &n) == T5_STREAM_OK && n == 2 && !memcmp(data, "BB", 2));
  assert(r.readRecord(owner, src, data, sizeof(data), &n) == T5_STREAM_EOF && n == 0);
  assert(r.closePipe(owner, p) == T5_STREAM_OK);
  assert(r.finish(owner, dst) == T5_STREAM_OK);
  assert(r.readRecord(owner, dst, data, sizeof(data), &n) == T5_STREAM_EOF);
  assert(r.recordInfo(owner, src, schema, sizeof(schema), &stats) == T5_STREAM_OK);
  assert(stats.records_read == 2 && stats.bytes_read == 3);

  // Empty records must not be confused with an empty pipe staging slot.
  auto emptySrc = records(r, "lora.packet.v1");
  auto emptyDst = records(r, "lora.packet.v1");
  assert(r.writeRecord(owner, emptySrc, nullptr, 0) == T5_STREAM_OK);
  assert(r.finish(owner, emptySrc) == T5_STREAM_OK);
  assert(r.connect(owner, emptySrc, emptyDst, 0, &p) == T5_STREAM_OK);
  r.pump();
  assert(r.readRecord(owner, emptyDst, data, sizeof(data), &n) == T5_STREAM_OK && n == 0);
  r.pump(); assert(pipeInfo(r, p).state == T5_PIPE_DONE);
  assert(r.closePipe(owner, p) == T5_STREAM_OK);

  // Source errors only become visible after its already queued records drain.
  auto failSrc = records(r, "lora.packet.v1");
  auto failDst = records(r, "lora.packet.v1");
  assert(r.writeRecord(owner, failSrc, "Q", 1) == T5_STREAM_OK);
  assert(r.finish(owner, failSrc, T5_STREAM_DISCONNECTED) == T5_STREAM_OK);
  assert(r.connect(owner, failSrc, failDst, 0, &p) == T5_STREAM_OK);
  r.pump();
  assert(r.readRecord(owner, failDst, data, sizeof(data), &n) == T5_STREAM_OK && n == 1 && data[0] == 'Q');
  r.pump();
  assert(pipeInfo(r, p).state == T5_PIPE_FAILED && pipeInfo(r, p).last_error == T5_STREAM_DISCONNECTED);

  const auto stale = src;
  r.release(owner);
  assert(r.readRecord(owner, stale, data, sizeof(data), &n) == T5_STREAM_INVALID);
  const auto replacement = records(r, "location.fix.v1");
  assert(replacement != stale);
  assert(r.recordInfo(owner, replacement, schema, sizeof(schema), &stats) == T5_STREAM_OK &&
         stats.records_written == 0 && stats.queued_records == 0);
  r.release(owner);
  std::cout << "Shared typed record stream/pipes tests passed\n";
}
