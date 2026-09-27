#include "PsramTlsAllocator.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>
#include <Logging.h>
#include <cstddef>
#include <cstdint>

namespace RuntimeNetwork {
namespace {
// The 16 KiB mbedTLS record buffers and large certificate working sets are
// ordinary byte-addressable storage. Keep small crypto/control allocations in
// internal memory for hardware-facing requirements.
constexpr size_t kPsramTlsMinimum = 4096;

void* tlsCalloc(size_t count, size_t bytes) {
  if (bytes && count > SIZE_MAX / bytes) return nullptr;
  const size_t total = count * bytes;
  if (total >= kPsramTlsMinimum)
    return heap_caps_calloc(count, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return heap_caps_calloc(count, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void tlsFree(void* pointer) { heap_caps_free(pointer); }
}  // namespace

void enablePsramTlsAllocations() {
  if (psramFound()) {
    // The bundled Arduino/IDF mbedTLS has PLATFORM_MEMORY enabled and exposes
    // this process-wide hook. Install before networking starts, never per request.
    if (mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree) == 0)
      LOG_INF("HTTP", "mbedTLS allocations >= 4096 bytes use PSRAM");
    else
      LOG_ERR("HTTP", "Could not install PSRAM allocator for mbedTLS");
  }
}

}  // namespace RuntimeNetwork
