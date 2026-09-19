#include "runtime/streams/StreamRuntime.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace RuntimeStreams;

int main() {
  Registry r;
  const uint32_t provider = 11, consumer = 22, stranger = 33;
  t5_stream_t src = 0, dst = 0, pipe = 0;
  uint32_t count = 0;
  uint8_t bytes[16]{};

  assert(r.publishEndpoint(provider, T5_STREAM_BYTES, T5_STREAM_READ | T5_STREAM_WRITE,
                           32, nullptr, 0, 0, kStreamPublic, &src) == T5_STREAM_OK);
  assert(r.publishEndpoint(consumer, T5_STREAM_BYTES, T5_STREAM_READ | T5_STREAM_WRITE,
                           32, nullptr, 0, 0, kStreamPublic, &dst) == T5_STREAM_OK);
  assert(r.write(provider, src, "hello", 5, &count) == T5_STREAM_OK && count == 5);
  assert(r.read(stranger, src, bytes, sizeof(bytes), &count) == T5_STREAM_INVALID);
  assert(r.write(stranger, src, "x", 1, &count) == T5_STREAM_INVALID);
  assert(r.connectAcross(consumer, provider, src, consumer, dst, 0, &pipe) == T5_STREAM_DENIED);
  assert(r.grant(provider, src, consumer, T5_STREAM_READ) == T5_STREAM_OK);
  assert(r.write(consumer, src, "x", 1, &count) == T5_STREAM_DENIED);
  assert(r.read(consumer, src, bytes, sizeof(bytes), &count) == T5_STREAM_OK && count == 5);
  assert(std::memcmp(bytes, "hello", 5) == 0);
  assert(r.write(provider, src, "pipe", 4, &count) == T5_STREAM_OK && count == 4);
  assert(r.connectAcross(stranger, provider, src, consumer, dst, 0, &pipe) == T5_STREAM_DENIED);
  assert(r.connectAcross(consumer, provider, src, consumer, dst, 0, &pipe) == T5_STREAM_OK);
  r.pump();
  assert(r.read(consumer, dst, bytes, sizeof(bytes), &count) == T5_STREAM_OK && count == 4);
  assert(std::memcmp(bytes, "pipe", 4) == 0);

  assert(r.revoke(provider, src, consumer) == T5_STREAM_OK);
  assert(r.read(consumer, src, bytes, 1, &count) == T5_STREAM_INVALID);
  t5_pipe_info_t info{}; info.struct_size = sizeof(info);
  assert(r.pipeInfo(consumer, pipe, &info) == T5_STREAM_OK);
  assert(info.state == T5_PIPE_FAILED && info.last_error == T5_STREAM_DISCONNECTED);
  assert(r.closePipe(consumer, pipe) == T5_STREAM_OK);

  // A WRITE-only grantee may write but not read. Generation and owner cannot
  // be inferred from a handle to bypass this explicit grant.
  t5_stream_t grantedWrite = 0;
  assert(r.publishEndpoint(provider, T5_STREAM_BYTES, T5_STREAM_WRITE | T5_STREAM_READ,
                           32, nullptr, 0, 0, kStreamPublic, &grantedWrite) == T5_STREAM_OK);
  assert(r.grant(provider, grantedWrite, consumer, T5_STREAM_WRITE) == T5_STREAM_OK);
  assert(r.read(consumer, grantedWrite, bytes, 1, &count) == T5_STREAM_DENIED);
  assert(r.write(consumer, grantedWrite, "x", 1, &count) == T5_STREAM_OK && count == 1);
  assert(r.read(provider, grantedWrite, bytes, 1, &count) == T5_STREAM_OK && count == 1);
  assert(bytes[0] == 'x');
  assert(r.revoke(provider, grantedWrite, consumer) == T5_STREAM_OK);
  assert(r.write(consumer, grantedWrite, "y", 1, &count) == T5_STREAM_INVALID);

  t5_stream_t prot = 0, openSink = 0;
  assert(r.publishEndpoint(provider, T5_STREAM_RECORDS, T5_STREAM_READ | T5_STREAM_WRITE,
                           0, "demo.v1", 16, 4, kStreamProtected, &prot) == T5_STREAM_OK);
  assert(r.publishEndpoint(consumer, T5_STREAM_RECORDS, T5_STREAM_READ | T5_STREAM_WRITE,
                           0, "demo.v1", 16, 4, kStreamPublic, &openSink) == T5_STREAM_OK);
  assert(r.grant(provider, prot, consumer, T5_STREAM_READ) == T5_STREAM_OK);
  assert(r.connectAcross(consumer, provider, prot, consumer, openSink, 0, &pipe) == T5_STREAM_DENIED);

  t5_stream_t protSink = 0;
  assert(r.publishEndpoint(consumer, T5_STREAM_RECORDS, T5_STREAM_READ | T5_STREAM_WRITE,
                           0, "demo.v1", 16, 4, kStreamProtected, &protSink) == T5_STREAM_OK);
  assert(r.writeRecord(provider, prot, "abc", 3) == T5_STREAM_OK);
  assert(r.connectAcross(consumer, provider, prot, consumer, protSink, 0, &pipe) == T5_STREAM_OK);
  r.pump();
  uint32_t size = 0;
  assert(r.readRecord(consumer, protSink, bytes, sizeof(bytes), &size) == T5_STREAM_OK && size == 3);

  r.release(consumer);
  r.release(provider);
  assert(r.read(provider, src, bytes, 1, &count) == T5_STREAM_INVALID);
  std::cout << "ELF endpoint tests passed\n";
}
