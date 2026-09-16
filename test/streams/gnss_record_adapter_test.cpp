#include "runtime/streams/GnssRecordAdapter.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

using namespace RuntimeStreams;
namespace {
uint32_t u32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
double f64(const uint8_t* p) {
  uint64_t bits = 0;
  for (unsigned i = 0; i < 8; ++i) bits |= static_cast<uint64_t>(p[i]) << (8 * i);
  double result = 0;
  std::memcpy(&result, &bits, sizeof(result));
  return result;
}
t5_pipe_info_t info(Registry& r, uint32_t owner, t5_pipe_t pipe) {
  t5_pipe_info_t result{};
  result.struct_size = sizeof(result);
  assert(r.pipeInfo(owner, pipe, &result) == T5_STREAM_OK);
  return result;
}
}
int main() {
  Registry registry;
  constexpr uint32_t owner = 117;
  t5_stream_t source = 55;
  assert(GnssRecordAdapter::open(registry, owner, &source, 2) == T5_STREAM_OK && source);
  t5_stream_info_t metadata{};
  metadata.struct_size = sizeof(metadata);
  assert(registry.info(owner, source, &metadata) == T5_STREAM_OK);
  assert(metadata.kind == T5_STREAM_RECORDS && metadata.flags == T5_STREAM_READ);
  assert(metadata.capacity == 2 * GnssRecordAdapter::Size);
  assert(registry.writeRecord(owner, source, "unauthorized", 12) == T5_STREAM_DENIED);
  assert(registry.produceRecord(owner + 1, source, "x", 1) == T5_STREAM_INVALID);

  t5_gps_state_t fix{};
  fix.status = T5_GPS_STATUS_FIX;
  fix.fix_valid = 1;
  fix.latitude = 44.532385;
  fix.longitude = -116.056066;
  fix.altitude_m = 1532.5f;
  fix.hdop = -1; // unknown, not a valid measurement
  fix.speed_kph = 0;
  fix.course_deg = 51.25f;
  fix.satellites = 11;
  fix.age_ms = 250;
  uint8_t wire[GnssRecordAdapter::Size]{};
  assert(GnssRecordAdapter::encode(fix, 1000, wire));
  assert(u32(wire + RISCRTE_FIX_OFFSET_VERSION) == 1);
  assert(u32(wire + RISCRTE_FIX_OFFSET_SAMPLE_MS) == 1000);
  assert(u32(wire + RISCRTE_FIX_OFFSET_FIX_MS) == 750);
  assert(u32(wire + RISCRTE_FIX_OFFSET_FLAGS) ==
         (RISCRTE_LOCATION_FIX_ALTITUDE_VALID | RISCRTE_LOCATION_FIX_SPEED_VALID |
          RISCRTE_LOCATION_FIX_HEADING_VALID));
  assert(f64(wire + RISCRTE_FIX_OFFSET_LATITUDE) == fix.latitude);
  assert(f64(wire + RISCRTE_FIX_OFFSET_LONGITUDE) == fix.longitude);
  assert(wire[RISCRTE_FIX_OFFSET_SATELLITES] == 11);
  assert(wire[49] == 0 && wire[50] == 0 && wire[51] == 0);
  assert(GnssRecordAdapter::publish(registry, owner, source, fix, 1000) == T5_STREAM_OK);
  assert(GnssRecordAdapter::publish(registry, owner, source, fix, 2000) == T5_STREAM_OK);
  assert(GnssRecordAdapter::publish(registry, owner, source, fix, 3000) == T5_STREAM_AGAIN);
  char schema[RecordQueue::MaxSchema]{};
  RecordQueue::Stats stats{};
  assert(registry.recordInfo(owner, source, schema, sizeof(schema), &stats) == T5_STREAM_OK);
  assert(std::strcmp(schema, RISCRTE_LOCATION_FIX_SCHEMA) == 0 &&
         stats.queued_records == 2 && stats.records_written == 2);
  uint32_t received = 123;
  uint8_t out[GnssRecordAdapter::Size]{};
  assert(registry.readRecord(owner, source, out, sizeof(out) - 1, &received) == T5_STREAM_LIMIT && !received);
  assert(registry.readRecord(owner, source, out, sizeof(out), &received) == T5_STREAM_OK &&
         received == sizeof(out) && std::memcmp(out, wire, sizeof(out)) == 0);
  assert(GnssRecordAdapter::publish(registry, owner, source, fix, 3000) == T5_STREAM_OK);
  assert(registry.readRecord(owner, source, out, sizeof(out), &received) == T5_STREAM_OK);
  assert(u32(out + RISCRTE_FIX_OFFSET_SAMPLE_MS) == 2000);
  assert(registry.readRecord(owner, source, out, sizeof(out), &received) == T5_STREAM_OK);
  assert(u32(out + RISCRTE_FIX_OFFSET_SAMPLE_MS) == 3000);

  fix.status = T5_GPS_STATUS_SEARCHING;
  assert(GnssRecordAdapter::publish(registry, owner, source, fix, 5000) == T5_STREAM_AGAIN);
  fix.status = T5_GPS_STATUS_FIX;
  fix.age_ms = 5001;
  assert(!GnssRecordAdapter::encode(fix, 5000, wire));
  fix.age_ms = 0;
  fix.latitude = std::numeric_limits<double>::quiet_NaN();
  assert(!GnssRecordAdapter::encode(fix, 5000, wire));
  fix.latitude = 91;
  assert(!GnssRecordAdapter::encode(fix, 5000, wire));
  fix.latitude = 44.532385;
  fix.longitude = -181;
  assert(!GnssRecordAdapter::encode(fix, 5000, wire));
  fix.longitude = -116.056066;
  fix.fix_valid = 0;
  assert(!GnssRecordAdapter::encode(fix, 5000, wire));
  fix.fix_valid = 1;
  fix.age_ms = 10;
  assert(GnssRecordAdapter::encode(fix, 5, wire));
  assert(u32(wire + RISCRTE_FIX_OFFSET_FIX_MS) == UINT32_MAX - 4);

  // Publish through the provider-only entry point, then through an ordinary
  // typed pipe; neither the read-only source nor app-visible v2 gains WRITE.
  t5_stream_t destination = 0;
  assert(registry.recordBuffer(owner, RISCRTE_LOCATION_FIX_SCHEMA,
                               GnssRecordAdapter::Size, 1, &destination) == T5_STREAM_OK);
  assert(GnssRecordAdapter::publish(registry, owner, source, fix, 5000) == T5_STREAM_OK);
  assert(registry.finish(owner, source) == T5_STREAM_OK);
  t5_pipe_t pipe = 0;
  assert(registry.connect(owner, source, destination, T5_PIPE_BLOCK_PRODUCER, &pipe) == T5_STREAM_OK);
  assert(registry.produceRecord(owner, source, wire, sizeof(wire)) == T5_STREAM_BUSY);
  registry.pump();
  assert(info(registry, owner, pipe).bytes_transferred == GnssRecordAdapter::Size);
  registry.pump();
  assert(info(registry, owner, pipe).state == T5_PIPE_DONE);
  assert(registry.readRecord(owner, destination, out, sizeof(out), &received) == T5_STREAM_OK &&
         received == GnssRecordAdapter::Size);
  assert(registry.closePipe(owner, pipe) == T5_STREAM_OK);
  const auto stale = source;
  registry.release(owner);
  assert(registry.produceRecord(owner, stale, wire, sizeof(wire)) == T5_STREAM_INVALID);
  assert(registry.readRecord(owner, destination, out, sizeof(out), &received) == T5_STREAM_INVALID);
  std::cout << "GNSS record adapter/provider publication tests passed\n";
}
