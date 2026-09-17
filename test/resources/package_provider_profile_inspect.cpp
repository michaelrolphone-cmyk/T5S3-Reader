// Integration test: REAL P-256 signer, canonical package, both resource hashes,
// bounded parser, independently pinned intake fingerprint. No hardware grant.
#include "runtime/packages/PackageProviderProfile.h"
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

namespace {
struct Sha256 {
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  ~Sha256() { EVP_MD_CTX_free(ctx); }
  bool start() { return ctx && EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1; }
  bool update(const uint8_t* bytes, size_t length) {
    return EVP_DigestUpdate(ctx, bytes, length) == 1;
  }
  bool finish(uint8_t out[32]) {
    unsigned count = 0;
    return EVP_DigestFinal_ex(ctx, out, &count) == 1 && count == 32;
  }
};
struct P256Verifier {
  EVP_PKEY* key = nullptr;
  explicit P256Verifier(const char* path) {
    FILE* file = std::fopen(path, "rb");
    if (file) { key = PEM_read_PUBKEY(file, nullptr, nullptr, nullptr); std::fclose(file); }
  }
  ~P256Verifier() { EVP_PKEY_free(key); }
  bool operator()(uint32_t id, const uint8_t digest[32],
                  const uint8_t signature[64]) const {
    if (id != 7 || !key || EVP_PKEY_base_id(key) != EVP_PKEY_EC) return false;
    ECDSA_SIG* pair = ECDSA_SIG_new();
    if (!pair) return false;
    BIGNUM* r = BN_bin2bn(signature, 32, nullptr);
    BIGNUM* s = BN_bin2bn(signature + 32, 32, nullptr);
    if (!r || !s || ECDSA_SIG_set0(pair, r, s) != 1) {
      BN_free(r); BN_free(s); ECDSA_SIG_free(pair); return false;
    }
    const int bytes = i2d_ECDSA_SIG(pair, nullptr);
    if (bytes < 1 || bytes > 80) { ECDSA_SIG_free(pair); return false; }
    uint8_t der[80]{};
    unsigned char* p = der;
    const bool encoded = i2d_ECDSA_SIG(pair, &p) == bytes;
    ECDSA_SIG_free(pair);
    if (!encoded) return false;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(key, nullptr);
    if (!ctx) return false;
    const bool valid = EVP_PKEY_verify_init(ctx) == 1 &&
      EVP_PKEY_CTX_set_signature_md(ctx, EVP_sha256()) == 1 &&
      EVP_PKEY_verify(ctx, der, static_cast<size_t>(bytes), digest, 32) == 1;
    EVP_PKEY_CTX_free(ctx);
    return valid;
  }
};
int nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}
} // namespace

int main(int argc, char** argv) {
  if (argc != 5) return 2; // package, public PEM, expected fingerprint, pass/reject
  std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
  if (!file) return 2;
  const auto end = file.tellg();
  if (end < 0) return 2;
  const uint64_t length = static_cast<uint64_t>(end);
  auto reader = [&file, length](uint64_t offset, uint8_t* dest, size_t count) {
    if (offset > length || count > length - offset) return false;
    file.clear();
    file.seekg(static_cast<std::streamoff>(offset));
    file.read(reinterpret_cast<char*>(dest), static_cast<std::streamsize>(count));
    return static_cast<size_t>(file.gcount()) == count;
  };
  if (std::strlen(argv[3]) != 64) return 2;
  uint8_t fingerprint[32]{};
  for (size_t i = 0; i < 32; ++i) {
    const int hi = nibble(argv[3][2 * i]), lo = nibble(argv[3][2 * i + 1]);
    if (hi < 0 || lo < 0) return 2;
    fingerprint[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  static RuntimePackages::PackageArchive archive{};
  static RuntimePackages::PackageVerificationWorkspace workspace{};
  static RuntimePackages::ProviderProfileReceipt receipt{};
  Sha256 sha;
  P256Verifier signer(argv[2]);
  const RuntimePackages::PackageRuntimePolicy policy{
      "xtensa-esp32s3", 2, 3, 1024 * 1024, 4 * 1024 * 1024};
  const auto result = RuntimePackages::verifySignedProviderProfile(
      reader, length, fingerprint, sha, signer,
      [](const char* name) -> uint32_t {
        return std::strcmp(name, "i2c.bus") == 0 ? 1u : 0u;
      }, [](const RuntimePackages::PackageArchive& a) -> bool {
        return a.securityVersion >= 3;
      }, policy, archive, workspace, receipt);
  const bool accepted = result == RuntimePackages::ProviderProfileResult::VerifiedSignedProfile;
  const bool expected = std::strcmp(argv[4], "pass") == 0;
  if (std::strcmp(argv[4], "pass") && std::strcmp(argv[4], "reject")) return 2;
  if (accepted != expected) {
    std::cerr << "Unexpected signed provider profile status " << static_cast<int>(result) << '\n';
    return 1;
  }
  if (accepted && (std::strcmp(receipt.identity.id, "usb-controller-test") ||
      std::strcmp(receipt.provides, "usb.controller") ||
      receipt.capabilityApi != 1 || receipt.requiredOsCpuAbi != 1 ||
      receipt.importCount != 3 || receipt.requirementCount != 1 ||
      receipt.securityVersion != 3 || receipt.signerKeyId != 7 ||
      receipt.executableLength != 64 ||
      std::strcmp(receipt.imports[0], "abort") ||
      std::strcmp(receipt.imports[1], "esp_intr_alloc") ||
      std::strcmp(receipt.imports[2], "malloc") ||
      std::strcmp(receipt.requirements[0].capability, "i2c.bus"))) {
    std::cerr << "Provider metadata receipt not complete or owned\n";
    return 1;
  }
  if (!accepted && (receipt.importCount || receipt.capabilityApi ||
                    receipt.executableLength || receipt.identity.id[0] ||
                    archive.entryCount)) {
    std::cerr << "Rejected profile retained stale metadata\n";
    return 1;
  }
  std::cout << (accepted ? "Signed provider profile admitted as metadata" :
                              "Signed provider profile rejected") << '\n';
}
