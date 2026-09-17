#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int mbedtls_sha256_ret(const unsigned char *data, size_t length,
                       unsigned char output[32], int is224);
#ifdef __cplusplus
}
#endif
