#pragma once
#include <openssl/sha.h>
inline int mbedtls_sha256_ret(const unsigned char* data, size_t size, unsigned char* out, int variant) {
  return !variant && SHA256(data, size, out) ? 0 : -1;
}
