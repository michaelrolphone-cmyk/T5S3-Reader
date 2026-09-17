#include "runtime/drivers/ProviderModuleV2.h"
#include <esp_elf.h>
#include <esp_heap_caps.h>
#include <mbedtls/sha256.h>
#include <openssl/sha.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static unsigned relocations = 0;
static unsigned allocations = 0;
static unsigned fallback_allocations = 0;
static bool reject_allocations = false;
static bool reject_spiram = false;
static bool reject_hash = false;
static const uint8_t* original = nullptr;
static uint8_t expected_payload[] = {0x7f, 'E', 'L', 'F', 1, 1, 1, 0x44, 0x19};

extern "C" void* heap_caps_malloc(size_t size, uint32_t capabilities) {
  ++allocations;
  if ((capabilities & MALLOC_CAP_SPIRAM) == 0) ++fallback_allocations;
  if (reject_allocations || (reject_spiram && (capabilities & MALLOC_CAP_SPIRAM))) return nullptr;
  return std::malloc(size);
}
extern "C" void heap_caps_free(void* ptr) { std::free(ptr); }
extern "C" int mbedtls_sha256_ret(const unsigned char* data, size_t length,
                                   unsigned char result[32], int is224) {
  if (reject_hash || is224) return -1;
  return SHA256(data, length, result) ? 0 : -1;
}
extern "C" int esp_elf_relocate_privileged_verified_v1(esp_elf_t* image,
                                                         const uint8_t* bytes, size_t length) {
  assert(image);
  ++relocations;
  /* The only permitted relocation input is a private copy of precisely the
   * digest-checked bytes. Deliberately fail before any native code can run. */
  assert(bytes != original);
  assert(length == sizeof(expected_payload));
  assert(std::memcmp(bytes, expected_payload, length) == 0);
  return -1;
}
extern "C" void esp_elf_deinit(esp_elf_t*) {}

static bool attempt(const uint8_t* image, size_t length, const uint8_t digest[32]) {
  RuntimeProviders::ModuleV2 module;
  return module.loadVerifiedBytes(image, length, digest, "fixture-signed",
                                  "cap.generic", 1, nullptr, 0);
}

int main() {
  uint8_t candidate[sizeof(expected_payload)]{};
  std::memcpy(candidate, expected_payload, sizeof(candidate));
  original = candidate;
  uint8_t signed_digest[32]{};
  assert(SHA256(expected_payload, sizeof(expected_payload), signed_digest));

  assert(!attempt(candidate, sizeof(candidate), signed_digest));
  assert(relocations == 1 && allocations == 1 && fallback_allocations == 0);

  candidate[7] ^= 0x80;
  assert(!attempt(candidate, sizeof(candidate), signed_digest));
  assert(relocations == 1); /* Tampered candidate denied before mapping. */
  candidate[7] ^= 0x80;
  uint8_t forged_digest[32]{};
  std::memcpy(forged_digest, signed_digest, sizeof(forged_digest));
  forged_digest[0] ^= 1;
  assert(!attempt(candidate, sizeof(candidate), forged_digest));
  assert(relocations == 1);

  reject_hash = true;
  assert(!attempt(candidate, sizeof(candidate), signed_digest));
  assert(relocations == 1);
  reject_hash = false;
  reject_allocations = true;
  assert(!attempt(candidate, sizeof(candidate), signed_digest));
  assert(relocations == 1);
  reject_allocations = false;

  reject_spiram = true;
  assert(!attempt(candidate, sizeof(candidate), signed_digest));
  assert(relocations == 2 && fallback_allocations >= 1);
  reject_spiram = false;
  assert(!attempt(nullptr, sizeof(candidate), signed_digest));
  assert(!attempt(candidate, sizeof(candidate), nullptr));
  assert(!attempt(candidate, 8u * 1024u * 1024u + 1u, signed_digest));
  assert(relocations == 2);
  std::puts("Privileged ELF: private snapshot, signed digest gate, failure paths PASS");
}
