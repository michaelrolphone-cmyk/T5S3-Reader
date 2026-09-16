#pragma once
#include "StreamRuntime.h"
#include <RiscRteLocationRecords.h>
#include <T5GpsApi.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace RuntimeStreams {

// The adapter consumes a COPY of a GNSS driver's state on the driver's owning
// task. It never stores any driver ELF pointers or calls into a driver from the
// stream scheduler. The caller serializes Registry access with its mutex.
class GnssRecordAdapter final {
 public:
  static constexpr uint32_t Size = RISCRTE_LOCATION_FIX_SIZE;
  static_assert(sizeof(float) == 4 && sizeof(double) == 8,
                "location.fix.v1 requires IEEE-754 binary32/binary64 storage");
  static_assert(std::numeric_limits<float>::is_iec559 &&
                std::numeric_limits<double>::is_iec559,
                "location.fix.v1 requires IEEE-754 floating point");

  // Caller owns the READ-only stream and must close it on provider/context
  // teardown. The provider writes exclusively via Registry::produceRecord.
  static int32_t open(Registry& registry, uint32_t owner, t5_stream_t* out,
                      uint32_t capacityRecords = 4) {
    return registry.recordBuffer(owner, RISCRTE_LOCATION_FIX_SCHEMA, Size,
                                 capacityRecords, out, T5_STREAM_READ);
  }

  // Only a valid, fresh fix becomes a semantic location record. A false return
  // means no record should be emitted; it is NOT a zero-coordinate fix.
  static bool encode(const t5_gps_state_t& fix, uint32_t sampleMs, uint8_t (&out)[Size]) {
    if (fix.status != T5_GPS_STATUS_FIX || !fix.fix_valid || fix.age_ms > 5000 ||
        !std::isfinite(fix.latitude) || !std::isfinite(fix.longitude) ||
        fix.latitude < -90 || fix.latitude > 90 ||
        fix.longitude < -180 || fix.longitude > 180) return false;
    std::memset(out, 0, Size);
    put32(out + RISCRTE_FIX_OFFSET_VERSION, RISCRTE_LOCATION_FIX_VERSION);
    put32(out + RISCRTE_FIX_OFFSET_SAMPLE_MS, sampleMs);
    put32(out + RISCRTE_FIX_OFFSET_FIX_MS, sampleMs - fix.age_ms);
    uint32_t flags = 0;
    if (std::isfinite(fix.altitude_m)) {
      putFloat(out + RISCRTE_FIX_OFFSET_ALTITUDE, fix.altitude_m);
      flags |= RISCRTE_LOCATION_FIX_ALTITUDE_VALID;
    }
    if (std::isfinite(fix.hdop) && fix.hdop >= 0) {
      putFloat(out + RISCRTE_FIX_OFFSET_HDOP, fix.hdop);
      flags |= RISCRTE_LOCATION_FIX_HDOP_VALID;
    }
    if (std::isfinite(fix.speed_kph) && fix.speed_kph >= 0) {
      putFloat(out + RISCRTE_FIX_OFFSET_SPEED, fix.speed_kph);
      flags |= RISCRTE_LOCATION_FIX_SPEED_VALID;
    }
    if (std::isfinite(fix.course_deg) && fix.course_deg >= 0 && fix.course_deg < 360) {
      putFloat(out + RISCRTE_FIX_OFFSET_HEADING, fix.course_deg);
      flags |= RISCRTE_LOCATION_FIX_HEADING_VALID;
    }
    put32(out + RISCRTE_FIX_OFFSET_FLAGS, flags);
    putDouble(out + RISCRTE_FIX_OFFSET_LATITUDE, fix.latitude);
    putDouble(out + RISCRTE_FIX_OFFSET_LONGITUDE, fix.longitude);
    out[RISCRTE_FIX_OFFSET_SATELLITES] = fix.satellites;
    return true;
  }

  static int32_t publish(Registry& registry, uint32_t owner, t5_stream_t stream,
                         const t5_gps_state_t& fix, uint32_t sampleMs) {
    uint8_t payload[Size]{};
    if (!encode(fix, sampleMs, payload)) return T5_STREAM_AGAIN;
    return registry.produceRecord(owner, stream, payload, sizeof(payload));
  }

 private:
  static void put32(uint8_t* out, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) out[i] = static_cast<uint8_t>(value >> (8 * i));
  }
  static void putFloat(uint8_t* out, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    put32(out, bits);
  }
  static void putDouble(uint8_t* out, double value) {
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    for (unsigned i = 0; i < 8; ++i) out[i] = static_cast<uint8_t>(bits >> (8 * i));
  }
};

}  // namespace RuntimeStreams
