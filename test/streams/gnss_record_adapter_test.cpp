#include "runtime/streams/GnssRecordAdapter.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
using namespace RuntimeStreams;
namespace {
uint32_t u32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
double f64(const uint8_t* p) {
  uint64_t bits = 0;
  for (unsigned i = 0; i < 8; ++i) bits |= uint64_t(p[i]) << (8 * i);
  double d = 0;
  std::memcpy(&d, &bits, sizeof(d));
  return d;
}
t5_pipe_info_t pipeInfo(Registry& r, uint32_t owner, t5_pipe_t pipe) {
  t5_pipe_info_t info{}; info.struct_size = sizeof(info);
  assert(r.pipeInfo(owner, pipe, &info) == T5_STREAM_OK); return info;
}
}
int main() {
  Registry r;
  constexpr uint32_t owner = 117;
  t5_stream_t source = 55;
  assert(GnssRecordAdapter::open(r, owner, &source, 2) == T5_STREAM_OK && source);
  t5_stream_info_t info{}; info.struct_size = sizeof(info);
  assert(r.info(owner, source, &info) == T5_STREAM_OK &&
         info.kind == T5_STREAM_RECORDS && info.flags == T5_STREAM_READ &&
         info.capacity == 2 * GnssRecordAdapter::Size);
  assert(r.writeRecord(owner, source, "x", 1) == T5_STREAM_DENIED);
  assert(r.produceRecord(owner + 1, source, "x", 1) == T5_STREAM_INVALID);

  t5_gps_state_t fix{};
  fix.status = T5_GPS_STATUS_FIX; fix.fix_valid = 1;
  fix.latitude = 44.532385; fix.longitude = -116.056066;
  fix.altitude_m = 1532.5f; fix.hdop = -1; fix.speed_kph = 0;
  fix.course_deg = 51.25f; fix.satellites = 11; fix.age_ms = 250;
  uint8_t wire[GnssRecordAdapter::Size]{};
  assert(GnssRecordAdapter::encode(fix, 1000, wire));
  assert(u32(wire + RISCRTE_FIX_OFFSET_VERSION) == 1 &&
         u32(wire + RISCRTE_FIX_OFFSET_SAMPLE_MS) == 1000 &&
         u32(wire + RISCRTE_FIX_OFFSET_FIX_MS) == 750);
  // The v1 GPS facade has no optional-field presence mask: never infer one
  // from default numeric zero, even for a plausible altitude or speed.
  assert(u32(wire + RISCRTE_FIX_OFFSET_FLAGS) == 0);
  assert(u32(wire + RISCRTE_FIX_OFFSET_ALTITUDE) == 0 &&
         u32(wire + RISCRTE_FIX_OFFSET_SPEED) == 0);
  assert(f64(wire + RISCRTE_FIX_OFFSET_LATITUDE) == fix.latitude &&
         f64(wire + RISCRTE_FIX_OFFSET_LONGITUDE) == fix.longitude);
  assert(wire[RISCRTE_FIX_OFFSET_SATELLITES] == 11 &&
         wire[49] == 0 && wire[50] == 0 && wire[51] == 0);
  uint8_t known[GnssRecordAdapter::Size]{};
  const uint32_t verified = RISCRTE_LOCATION_FIX_ALTITUDE_VALID |
                            RISCRTE_LOCATION_FIX_HDOP_VALID |
                            RISCRTE_LOCATION_FIX_SPEED_VALID |
                            RISCRTE_LOCATION_FIX_HEADING_VALID;
  assert(GnssRecordAdapter::encode(fix, 1000, known, verified));
  assert(u32(known + RISCRTE_FIX_OFFSET_FLAGS) == (verified & ~RISCRTE_LOCATION_FIX_HDOP_VALID));
  assert(!GnssRecordAdapter::encode(fix, 1000, known, 0x80000000u));

  assert(GnssRecordAdapter::publish(r, owner, source, fix, 1000) == T5_STREAM_OK);
  assert(GnssRecordAdapter::publish(r, owner, source, fix, 2000) == T5_STREAM_OK);
  assert(GnssRecordAdapter::publish(r, owner, source, fix, 3000) == T5_STREAM_AGAIN);
  char schema[RecordQueue::MaxSchema]{}; RecordQueue::Stats stats{};
  assert(r.recordInfo(owner, source, schema, sizeof(schema), &stats) == T5_STREAM_OK &&
         std::strcmp(schema, RISCRTE_LOCATION_FIX_SCHEMA) == 0 &&
         stats.queued_records == 2 && stats.records_written == 2);
  uint8_t out[GnssRecordAdapter::Size]{}; uint32_t n = 99;
  assert(r.readRecord(owner, source, out, sizeof(out) - 1, &n) == T5_STREAM_LIMIT && !n);
  assert(r.readRecord(owner, source, out, sizeof(out), &n) == T5_STREAM_OK &&
         n == sizeof(out) && std::memcmp(out, wire, sizeof(out)) == 0);
  assert(GnssRecordAdapter::publish(r, owner, source, fix, 3000) == T5_STREAM_OK);
  assert(r.readRecord(owner, source, out, sizeof(out), &n) == T5_STREAM_OK &&
         u32(out + RISCRTE_FIX_OFFSET_SAMPLE_MS) == 2000);
  assert(r.readRecord(owner, source, out, sizeof(out), &n) == T5_STREAM_OK &&
         u32(out + RISCRTE_FIX_OFFSET_SAMPLE_MS) == 3000);

  fix.status = T5_GPS_STATUS_SEARCHING;
  assert(GnssRecordAdapter::publish(r, owner, source, fix, 5000) == T5_STREAM_AGAIN);
  fix.status = T5_GPS_STATUS_FIX; fix.age_ms = 5001;
  assert(!GnssRecordAdapter::encode(fix, 5000, wire));
  fix.age_ms = 0; fix.latitude = std::numeric_limits<double>::quiet_NaN();
  assert(!GnssRecordAdapter::encode(fix, 5000, wire));
  fix.latitude = 91; assert(!GnssRecordAdapter::encode(fix, 5000, wire));
  fix.latitude = 44.532385; fix.longitude = -181;
  assert(!GnssRecordAdapter::encode(fix, 5000, wire));
  fix.longitude = -116.056066; fix.fix_valid = 0;
  assert(!GnssRecordAdapter::encode(fix, 5000, wire));
  fix.fix_valid = 1; fix.age_ms = 10;
  assert(GnssRecordAdapter::encode(fix, 5, wire) &&
         u32(wire + RISCRTE_FIX_OFFSET_FIX_MS) == UINT32_MAX - 4);

  t5_stream_t destination = 0;
  assert(r.recordBuffer(owner, RISCRTE_LOCATION_FIX_SCHEMA,
                        GnssRecordAdapter::Size, 1, &destination) == T5_STREAM_OK);
  t5_pipe_t pipe = 0;
  assert(r.connect(owner, source, destination, T5_PIPE_BLOCK_PRODUCER, &pipe) == T5_STREAM_OK);
  assert(r.writeRecord(owner, source, wire, sizeof(wire)) == T5_STREAM_DENIED);
  assert(r.writeRecord(owner, destination, wire, sizeof(wire)) == T5_STREAM_BUSY);
  assert(GnssRecordAdapter::publish(r, owner, source, fix, 5000) == T5_STREAM_OK);
  assert(r.finish(owner, source) == T5_STREAM_OK);
  r.pump(); assert(pipeInfo(r, owner, pipe).bytes_transferred == GnssRecordAdapter::Size);
  r.pump(); assert(pipeInfo(r, owner, pipe).state == T5_PIPE_DONE);
  assert(r.readRecord(owner, destination, out, sizeof(out), &n) == T5_STREAM_OK && n == sizeof(out));
  assert(r.closePipe(owner, pipe) == T5_STREAM_OK);
  const auto stale = source;
  r.release(owner);
  assert(r.produceRecord(owner, stale, wire, sizeof(wire)) == T5_STREAM_INVALID);
  assert(r.readRecord(owner, destination, out, sizeof(out), &n) == T5_STREAM_INVALID);
  std::cout << "GNSS record adapter/provider publication tests passed\n";
}
