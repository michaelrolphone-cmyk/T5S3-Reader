// Integration fixture: verify, stage and reverify a real signed archive from
// scripts/build_risc_package.py; corrupting the sealed copy must discard it.
#include "runtime/packages/PackageArchiveStage.h"
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <cstdint>
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
  bool update(const uint8_t* data, size_t n) { return EVP_DigestUpdate(ctx, data, n) == 1; }
  bool finish(uint8_t digest[32]) { unsigned bytes = 0; return EVP_DigestFinal_ex(ctx, digest, &bytes) == 1 && bytes == 32; }
};
struct P256Verifier {
  EVP_PKEY* publicKey = nullptr;
  explicit P256Verifier(const char* path) {
    FILE* file = std::fopen(path, "rb");
    if (file) { publicKey = PEM_read_PUBKEY(file, nullptr, nullptr, nullptr); std::fclose(file); }
  }
  ~P256Verifier() { EVP_PKEY_free(publicKey); }
  P256Verifier(const P256Verifier&) = delete;
  P256Verifier& operator=(const P256Verifier&) = delete;
  bool operator()(uint32_t keyId, const uint8_t digest[32], const uint8_t signature[64]) const {
    if (keyId != 7 || !publicKey || EVP_PKEY_base_id(publicKey) != EVP_PKEY_EC) return false;
    ECDSA_SIG* structured = ECDSA_SIG_new();
    if (!structured) return false;
    BIGNUM* r = BN_bin2bn(signature, 32, nullptr);
    BIGNUM* s = BN_bin2bn(signature + 32, 32, nullptr);
    if (!r || !s || ECDSA_SIG_set0(structured, r, s) != 1) {
      BN_free(r); BN_free(s); ECDSA_SIG_free(structured); return false;
    }
    const int length = i2d_ECDSA_SIG(structured, nullptr);
    if (length <= 0 || length > 80) { ECDSA_SIG_free(structured); return false; }
    uint8_t der[80]{};
    unsigned char* write = der;
    const bool encoded = i2d_ECDSA_SIG(structured, &write) == length;
    ECDSA_SIG_free(structured);
    if (!encoded) return false;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(publicKey, nullptr);
    if (!ctx) return false;
    const bool accepted = EVP_PKEY_verify_init(ctx) == 1 &&
        EVP_PKEY_CTX_set_signature_md(ctx, EVP_sha256()) == 1 &&
        EVP_PKEY_verify(ctx, der, static_cast<size_t>(length), digest, 32) == 1;
    EVP_PKEY_CTX_free(ctx);
    return accepted;
  }
};
struct Stage {
  std::vector<uint8_t> bytes;
  uint64_t expected = 0;
  bool sealed = false;
  bool alter = false;
  bool begin(uint64_t length) {
    if (!bytes.empty()) return false;
    expected = length;
    return true;
  }
  bool append(const uint8_t* data, size_t count) {
    if (sealed || count > expected - bytes.size()) return false;
    bytes.insert(bytes.end(), data, data + count);
    return true;
  }
  bool seal() {
    if (bytes.size() != expected) return false;
    sealed = true;
    if (alter) bytes.back() ^= 1;
    return true;
  }
  bool readAt(uint64_t offset, uint8_t* out, size_t count) {
    if (!sealed || offset > bytes.size() || count > bytes.size() - offset) return false;
    std::memcpy(out, bytes.data() + offset, count);
    return true;
  }
  bool discard() { bytes.clear(); sealed = false; return true; }
};
} // namespace

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
  if (!file) return 2;
  const auto end = file.tellg();
  if (end < 0) return 2;
  const uint64_t length = static_cast<uint64_t>(end);
  auto reader = [&file, length](uint64_t offset, uint8_t* dest, size_t count) {
    if (offset > length || count > length - offset) return false;
    file.clear(); file.seekg(static_cast<std::streamoff>(offset));
    file.read(reinterpret_cast<char*>(dest), static_cast<std::streamsize>(count));
    return file.good() && static_cast<size_t>(file.gcount()) == count;
  };
  static RuntimePackages::PackageVerificationWorkspace workspace{};
  RuntimePackages::PackageArchive archive{};
  Sha256 sha;
  P256Verifier signer(argv[2]);
  const RuntimePackages::PackageRuntimePolicy policy{"xtensa-esp32s3", 2, 3, 1024 * 1024, 4 * 1024 * 1024};
  Stage stage;
  const auto result = RuntimePackages::stageSignedPackageArchive(
      reader, length, stage, sha, signer,
      [](const char* name) -> uint32_t {
        return std::strcmp(name, "kernel.serial") == 0 ? 1u : 0u;
      }, policy, archive, workspace);
  if (result != RuntimePackages::ArchiveStageResult::ReadyForPublicationReview) {
    std::cerr << "Archive stage rejected at " << static_cast<int>(result) << '\n';
    return 1;
  }
  Stage badStage;
  badStage.alter = true;
  RuntimePackages::PackageArchive rejected{};
  const auto tampered = RuntimePackages::stageSignedPackageArchive(
      reader, length, badStage, sha, signer,
      [](const char* name) -> uint32_t {
        return std::strcmp(name, "kernel.serial") == 0 ? 1u : 0u;
      }, policy, rejected, workspace);
  if (tampered != RuntimePackages::ArchiveStageResult::StageUntrusted ||
      !badStage.bytes.empty()) {
    std::cerr << "Mutated stage was not rejected and discarded\n";
    return 1;
  }
  std::cout << "Authenticated, staged and tamper-tested " << archive.entryCount << " entries\n";
}
