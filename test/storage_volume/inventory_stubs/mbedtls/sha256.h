#pragma once
#include <openssl/sha.h>
#include <openssl/evp.h>
inline size_t receiptTestHashBytes = 0;
inline void (*receiptTestHashHook)() = nullptr;
struct mbedtls_sha256_context { EVP_MD_CTX* state = nullptr; };
inline void mbedtls_sha256_init(mbedtls_sha256_context* c) { c->state = EVP_MD_CTX_new(); }
inline void mbedtls_sha256_free(mbedtls_sha256_context* c) { EVP_MD_CTX_free(c->state);c->state=nullptr; }
inline int mbedtls_sha256_starts_ret(mbedtls_sha256_context* c,int variant) {
  return !variant&&c->state&&EVP_DigestInit_ex(c->state,EVP_sha256(),nullptr)==1?0:-1;
}
inline int mbedtls_sha256_update_ret(mbedtls_sha256_context* c,const unsigned char* p,size_t n) {
  receiptTestHashBytes+=n;if (receiptTestHashHook) receiptTestHashHook();return EVP_DigestUpdate(c->state,p,n)==1?0:-1;
}
inline int mbedtls_sha256_finish_ret(mbedtls_sha256_context* c,unsigned char* out) {
  unsigned n=0;return EVP_DigestFinal_ex(c->state,out,&n)==1&&n==32?0:-1;
}
inline int mbedtls_sha256_ret(const unsigned char* data, size_t size, unsigned char* out, int variant) {
  receiptTestHashBytes+=size;return !variant && SHA256(data, size, out) ? 0 : -1;
}
