#include "runtime/streams/RecordQueue.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
using namespace RuntimeStreams;

int main() {
  assert(RecordQueue::validSchema("location.fix.v1"));
  assert(RecordQueue::validSchema("network.udp.datagram.v20"));
  assert(RecordQueue::validSchema("a.v1"));
  assert(RecordQueue::validSchema("device_2.sensor-name.v12"));
  for (const char* invalid : {"", "location.fix", "location.fix.v0", "location.fix.v1.",
                              "location..fix.v1", "Location.fix.v1", "location.fix.v1?",
                              "location.fix.v1x", "location.fix.v1.extra", "location.fix.v1.v2",
                              "location.v12.fix.v2", "v1.location.v2", "1location.fix.v1",
                              "location.1fix.v1", "location.-fix.v1", "location.fix.v01",
                              "location.fix.v-1", "location.fix.v1.2", "location.fix.v1.v0"})
    assert(!RecordQueue::validSchema(invalid));
  assert(!RecordQueue::validSchema(std::string(64, 'a').c_str()));
  assert(RecordQueue::compatible("location.fix.v1", "location.fix.v1"));
  assert(!RecordQueue::compatible("location.fix.v1", "location.fix.v2"));
  assert(!RecordQueue::compatible("location.fix.v1", "sensor.measurement.v1"));
  assert(!RecordQueue::compatible("location.fix.v1", ""));

  RecordQueue q;
  uint32_t size = 99;
  char out[512]{};
  assert(q.write("x", 1) == T5_STREAM_INVALID);
  assert(q.read(out, sizeof(out), &size) == T5_STREAM_INVALID && size == 0);
  assert(q.configure("location.fix", 32, 3) == T5_STREAM_INVALID);
  assert(q.configure("location.fix.v1.v2", 32, 3) == T5_STREAM_INVALID);
  assert(q.configure("location.fix.v1", 513, 1) == T5_STREAM_INVALID);
  assert(q.configure("location.fix.v1", 32, 9) == T5_STREAM_INVALID);
  assert(q.configure("location.fix.v1", 512, 9) == T5_STREAM_INVALID);
  assert(q.configure("location.fix.v1", 512, 8) == T5_STREAM_OK);
  assert(q.configure("location.fix.v2", 32, 2) == T5_STREAM_BUSY);
  assert(!std::strcmp(q.schema(), "location.fix.v1"));
  assert(q.read(out, sizeof(out), &size) == T5_STREAM_AGAIN && size == 0);
  assert(q.write(nullptr, 1) == T5_STREAM_INVALID);
  assert(q.write("x", 513) == T5_STREAM_INVALID);

  // The destination cannot consume a record prefix. A short receive buffer
  // leaves the oldest queued record entirely intact.
  assert(q.write("abc", 3) == T5_STREAM_OK);
  assert(q.write(nullptr, 0) == T5_STREAM_OK); // Empty record is not AGAIN.
  assert(q.write("xyz12", 5) == T5_STREAM_OK);
  assert(q.read(out, 2, &size) == T5_STREAM_LIMIT && size == 0 && q.queued() == 3);
  assert(q.read(out, 3, &size) == T5_STREAM_OK && size == 3 && !memcmp(out, "abc", 3));
  assert(q.read(out, 0, &size) == T5_STREAM_OK && size == 0);
  assert(q.read(out, 5, &size) == T5_STREAM_OK && size == 5 && !memcmp(out, "xyz12", 5));

  // FIFO wraparound and full-queue backpressure cannot silently drop frames.
  for (int i = 0; i < 8; ++i) {
    const char c = static_cast<char>('a' + i);
    assert(q.write(&c, 1) == T5_STREAM_OK);
  }
  assert(q.write("overflow", 8) == T5_STREAM_AGAIN && q.queued() == 8);
  for (int i = 0; i < 8; ++i)
    assert(q.read(out, 1, &size) == T5_STREAM_OK && size == 1 && out[0] == 'a' + i);

  // Terminal is visible only after draining already buffered complete frames.
  assert(q.write("last", 4) == T5_STREAM_OK);
  assert(q.finish() == T5_STREAM_OK);
  assert(q.write("no", 2) == T5_STREAM_CLOSED);
  assert(q.read(out, sizeof(out), &size) == T5_STREAM_OK && size == 4 && !memcmp(out, "last", 4));
  assert(q.read(out, sizeof(out), &size) == T5_STREAM_EOF && size == 0);
  const auto stats = q.stats();
  assert(stats.records_written == 12 && stats.records_read == 12);
  assert(stats.bytes_written == stats.bytes_read && stats.bytes_written == 20);
  assert(stats.high_water_records == 8 && stats.queued_records == 0);
  assert(stats.terminal == T5_STREAM_EOF);

  q.reset();
  assert(!q.configured() && q.schema() == nullptr && q.stats().records_written == 0);
  assert(q.configure("lora.packet.v1", 4, 2) == T5_STREAM_OK);
  assert(q.write("A", 1) == T5_STREAM_OK);
  assert(q.finish(T5_STREAM_DISCONNECTED) == T5_STREAM_OK);
  assert(q.read(out, 4, &size) == T5_STREAM_OK && size == 1 && out[0] == 'A');
  assert(q.read(out, 4, &size) == T5_STREAM_DISCONNECTED && size == 0);
  assert(q.write("B", 1) == T5_STREAM_DISCONNECTED);
  q.reset();
  assert(q.configure("sensor.measurement.v1", 512, 8) == T5_STREAM_OK);
  char payload[512]{};
  assert(q.write(payload, 512) == T5_STREAM_OK);
  assert(q.read(payload, 511, &size) == T5_STREAM_LIMIT && q.queued() == 1);
  assert(q.read(payload, 512, &size) == T5_STREAM_OK && size == 512);
  std::cout << "Typed record queue foundation tests passed\n";
}
