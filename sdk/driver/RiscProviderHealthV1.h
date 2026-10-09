#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RISC_PROVIDER_HEALTH_API_V1 1u
enum risc_provider_health_status_v1 {
  RISC_PROVIDER_HEALTH_RETAINED = -1,
  RISC_PROVIDER_HEALTH_UNKNOWN = 0,
  RISC_PROVIDER_HEALTH_READY = 1,
  RISC_PROVIDER_HEALTH_BUSY = 2
};

/* Optional, separately exported read-only const object named
 * risc_provider_health_v1_descriptor. This does not extend any driver ABI.
 * check is bounded and observational: no polling, cleanup, native state
 * changes or graph reentry. READY attests known safe transition state, with no
 * references, callbacks or borrowed buffers into the departing invocation's
 * owned memory. A surviving provider must own everything it continues to use.
 * BUSY means reversible work remains; UNKNOWN refuses transition; RETAINED
 * means terminal custody loss. Missing/malformed descriptors are UNKNOWN.
 * The host never calls this mapping again after terminal custody loss. */
typedef struct risc_provider_health_v1 {
  uint32_t api_version;
  uint32_t struct_size;
  int32_t (*check)(void);
} risc_provider_health_v1;

#ifdef __cplusplus
}
#endif
