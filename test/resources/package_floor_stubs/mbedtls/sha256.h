#pragma once

#include <cstddef>
#include <openssl/sha.h>

inline int mbedtls_sha256_ret(const unsigned char* data, size_t length,
                              unsigned char digest[32], int is224) {
  if (is224 || !data || !digest) return -1;
  return SHA256(data, length, digest) ? 0 : -1;
}
