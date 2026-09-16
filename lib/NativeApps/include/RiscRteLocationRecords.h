#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RiscRTE location.fix.v1: one atomic, fixed-length 52-byte record.
 * All multibyte fields are little-endian IEEE-754 or unsigned integers;
 * consumers must decode offsets rather than casting the bytes to a struct.
 * sample_ms and fix_ms use the same wrapping monotonic 32-bit clock domain.
 * GNSS UTC is not implied. A fix is published only when the provider reports
 * a currently valid, at-most-five-second-old position.
 */
#define RISCRTE_LOCATION_FIX_SCHEMA "location.fix.v1"
#define RISCRTE_LOCATION_FIX_VERSION 1u
#define RISCRTE_LOCATION_FIX_SIZE 52u
#define RISCRTE_LOCATION_FIX_ALTITUDE_VALID  (1u << 0)
#define RISCRTE_LOCATION_FIX_HDOP_VALID      (1u << 1)
#define RISCRTE_LOCATION_FIX_SPEED_VALID     (1u << 2)
#define RISCRTE_LOCATION_FIX_HEADING_VALID   (1u << 3)

enum {
  RISCRTE_FIX_OFFSET_VERSION = 0,
  RISCRTE_FIX_OFFSET_SAMPLE_MS = 4,
  RISCRTE_FIX_OFFSET_FIX_MS = 8,
  RISCRTE_FIX_OFFSET_FLAGS = 12,
  RISCRTE_FIX_OFFSET_LATITUDE = 16,
  RISCRTE_FIX_OFFSET_LONGITUDE = 24,
  RISCRTE_FIX_OFFSET_ALTITUDE = 32,
  RISCRTE_FIX_OFFSET_HDOP = 36,
  RISCRTE_FIX_OFFSET_SPEED = 40,
  RISCRTE_FIX_OFFSET_HEADING = 44,
  RISCRTE_FIX_OFFSET_SATELLITES = 48,
  RISCRTE_FIX_OFFSET_RESERVED = 49
};

#ifdef __cplusplus
}
#endif
