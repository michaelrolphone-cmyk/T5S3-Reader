#pragma once
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <openssl/evp.h>

inline std::vector<uint8_t> imageBytes, writtenBytes;
inline size_t readBytes = 0, readCalls = 0, closeCalls = 0, openCount = 0, hashCount = 0;
inline size_t hashInitCalls = 0;
inline size_t eraseCalls = 0, writeCalls = 0, switchCalls = 0;
inline int failReadAt = -1;
inline bool failOpen = false, failSeek = false, noPartition = false, failErase = false, failWrite = false, failSwitch = false;
class HalFile {
  bool opened = false;
  size_t pos = 0;
public:
  ~HalFile() { close(); }
  void open() { assert(!opened); opened = true; pos = 0; ++openCount; }
  explicit operator bool() const { return opened; }
  size_t fileSize() const { return imageBytes.size(); }
  bool available() const { return opened && pos < imageBytes.size(); }
  bool seekSet(size_t n) { if (failSeek || n > imageBytes.size()) return false; pos = n; return true; }
  int read(void* dst, size_t n) {
    assert(opened); const auto call = readCalls++;
    if (failReadAt >= 0 && call == static_cast<size_t>(failReadAt)) return -1;
    n = std::min(n, imageBytes.size() - pos); std::memcpy(dst, imageBytes.data() + pos, n); pos += n; readBytes += n;
    return static_cast<int>(n);
  }
  void close() { if (opened) { opened = false; --openCount; ++closeCalls; } }
};
struct StorageStub { bool openFileForRead(const char*, const char*, HalFile& f) { if (failOpen) return false; f.open(); return true; } };
inline StorageStub Storage;
namespace Board { inline const char* id() { return "fixture"; } inline const char* firmwareMarker() { return "RISCRTE_BOARD_ID:fixture"; } }
inline void delay(unsigned) {}
inline void esp_task_wdt_reset() {}
#define LOG_ERR(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define ESP_OK 0
#define SPI_FLASH_SEC_SIZE 4096
struct esp_partition_t { size_t size; const char* label; uint32_t address; };
inline esp_partition_t partition{1024 * 1024, "fixture", 0x10000};
inline const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) { return noPartition ? nullptr : &partition; }
inline int esp_partition_erase_range(const esp_partition_t*, size_t, size_t) { ++eraseCalls; return failErase ? -1 : ESP_OK; }
inline int esp_partition_write(const esp_partition_t*, size_t offset, const void* data, size_t n) {
  ++writeCalls; if (failWrite) return -1;
  assert(offset == writtenBytes.size()); const auto* p = static_cast<const uint8_t*>(data); writtenBytes.insert(writtenBytes.end(), p, p+n); return ESP_OK;
}
struct mbedtls_sha256_context { EVP_MD_CTX* ctx = nullptr; };
inline void mbedtls_sha256_init(mbedtls_sha256_context* c) { c->ctx = EVP_MD_CTX_new(); assert(c->ctx); ++hashCount; ++hashInitCalls; }
inline void mbedtls_sha256_starts(mbedtls_sha256_context* c, int is224) { assert(!is224); assert(EVP_DigestInit_ex(c->ctx, EVP_sha256(), nullptr) == 1); }
inline void mbedtls_sha256_update(mbedtls_sha256_context* c, const uint8_t* bytes, size_t n) { assert(EVP_DigestUpdate(c->ctx, bytes, n) == 1); }
inline void mbedtls_sha256_finish(mbedtls_sha256_context* c, uint8_t* out) { unsigned n = 0; assert(EVP_DigestFinal_ex(c->ctx, out, &n) == 1 && n == 32); }
inline void mbedtls_sha256_free(mbedtls_sha256_context* c) { assert(c->ctx); EVP_MD_CTX_free(c->ctx); c->ctx = nullptr; --hashCount; }
