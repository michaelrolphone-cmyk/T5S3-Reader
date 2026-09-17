#include "runtime/packages/PackageArchiveStage.h"

#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

using namespace RuntimePackages;

namespace {
std::vector<uint8_t> load(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

struct Sha256 {
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  ~Sha256() { EVP_MD_CTX_free(ctx); }
  bool start() { return ctx && EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1; }
  bool update(const uint8_t* data, size_t n) {
    return EVP_DigestUpdate(ctx, data, n) == 1;
  }
  bool finish(uint8_t digest[32]) {
    unsigned count = 0;
    return EVP_DigestFinal_ex(ctx, digest, &count) == 1 && count == 32;
  }
};

struct Signer {
  EVP_PKEY* key = nullptr;
  explicit Signer(const char* path) {
    FILE* file = std::fopen(path, "rb");
    assert(file);
    key = PEM_read_PUBKEY(file, nullptr, nullptr, nullptr);
    std::fclose(file);
    assert(key);
  }
  ~Signer() { EVP_PKEY_free(key); }
  bool operator()(uint32_t id, const uint8_t digest[32], const uint8_t signature[64]) const {
    if (id != 7) return false;
    ECDSA_SIG* parsed = ECDSA_SIG_new();
    if (!parsed) return false;
    BIGNUM* r = BN_bin2bn(signature, 32, nullptr);
    BIGNUM* s = BN_bin2bn(signature + 32, 32, nullptr);
    if (!r || !s || ECDSA_SIG_set0(parsed, r, s) != 1) {
      BN_free(r); BN_free(s); ECDSA_SIG_free(parsed); return false;
    }
    const int derLength = i2d_ECDSA_SIG(parsed, nullptr);
    uint8_t der[80]{};
    unsigned char* cursor = der;
    const bool encoded = derLength > 0 && derLength <= static_cast<int>(sizeof(der)) &&
        i2d_ECDSA_SIG(parsed, &cursor) == derLength;
    ECDSA_SIG_free(parsed);
    if (!encoded) return false;
    EVP_PKEY_CTX* context = EVP_PKEY_CTX_new(key, nullptr);
    if (!context) return false;
    const bool accepted = EVP_PKEY_verify_init(context) == 1 &&
        EVP_PKEY_CTX_set_signature_md(context, EVP_sha256()) == 1 &&
        EVP_PKEY_verify(context, der, static_cast<size_t>(derLength), digest, 32) == 1;
    EVP_PKEY_CTX_free(context);
    return accepted;
  }
};

struct Stage {
  std::vector<uint8_t> bytes;
  std::vector<uint8_t>* source = nullptr;
  const std::vector<uint8_t>* swap = nullptr;
  bool failBegin = false, failAppend = false, failSeal = false, corruptOnSeal = false;
  bool mutateSourceOnBegin = false, began = false, sealed = false, discarded = false;
  size_t appends = 0;
  bool begin(uint64_t length) {
    if (failBegin) return false;
    began = true;
    bytes.clear();
    assert(length < 1024 * 1024);
    bytes.reserve(static_cast<size_t>(length));
    if (swap) *source = *swap; // Valid signed package substituted AFTER source authentication.
    if (mutateSourceOnBegin) source->back() ^= 1;
    return true;
  }
  bool append(const uint8_t* data, size_t length) {
    ++appends;
    if (failAppend && appends == 2) return false;
    bytes.insert(bytes.end(), data, data + length);
    return true;
  }
  bool seal() {
    if (failSeal) return false;
    sealed = true;
    if (corruptOnSeal) bytes.back() ^= 1;
    return true;
  }
  bool readAt(uint64_t at, uint8_t* dest, size_t length) {
    if (!sealed || at > bytes.size() || length > bytes.size() - static_cast<size_t>(at))
      return false;
    std::memcpy(dest, bytes.data() + at, length);
    return true;
  }
  bool discard() { discarded = true; bytes.clear(); sealed = false; return true; }
};

ArchiveStageResult exercise(std::vector<uint8_t>& source, Stage& stage,
                            Signer& signer, PackageRuntimePolicy policy =
                                {"xtensa-esp32s3", 2, 3, 1024 * 1024, 4 * 1024 * 1024}) {
  stage.source = &source;
  auto sourceRead = [&source](uint64_t at, uint8_t* dest, size_t length) {
    if (at > source.size() || length > source.size() - static_cast<size_t>(at)) return false;
    std::memcpy(dest, source.data() + at, length);
    return true;
  };
  Sha256 sha;
  static PackageVerificationWorkspace workspace{};
  static PackageArchive archive{};
  const auto result = stageSignedPackageArchive(sourceRead, source.size(), stage, sha,
      signer, [](const char* name) -> uint32_t {
        return std::strcmp(name, "kernel.serial") == 0 ? 1u : 0u;
      }, policy, archive, workspace);
  if (result == ArchiveStageResult::ReadyForPublicationReview) {
    assert(std::strcmp(archive.identity.id, "gps-nmea") == 0);
    assert(archive.entryCount == 2 && stage.sealed && !stage.discarded);
    assert(stage.bytes == source);
  } else {
    assert(!archive.identity.id[0]); // Failure cannot leak an authenticated identity.
  }
  return result;
}
} // namespace

int main(int argc, char** argv) {
  assert(argc == 4);
  const auto good = load(argv[1]);
  const auto alternate = load(argv[2]);
  assert(good.size() == alternate.size() && good != alternate);
  Signer signer(argv[3]);
  {
    auto source = good; Stage stage;
    assert(exercise(source, stage, signer) == ArchiveStageResult::ReadyForPublicationReview);
  }
  {
    auto source = good; source.back() ^= 1; Stage stage;
    assert(exercise(source, stage, signer) == ArchiveStageResult::SourceUntrusted);
    assert(!stage.began);
  }
  {
    auto source = good; Stage stage;
    const PackageRuntimePolicy incompatible{"riscv32", 2, 3, 1024 * 1024, 4 * 1024 * 1024};
    assert(exercise(source, stage, signer, incompatible) == ArchiveStageResult::PreflightRejected);
    assert(!stage.began);
  }
  {
    auto source = good; Stage stage; stage.failBegin = true;
    assert(exercise(source, stage, signer) == ArchiveStageResult::StageUnavailable);
  }
  {
    auto source = good; Stage stage; stage.failAppend = true;
    assert(exercise(source, stage, signer) == ArchiveStageResult::CopyFailure);
    assert(stage.discarded);
  }
  {
    auto source = good; Stage stage; stage.failSeal = true;
    assert(exercise(source, stage, signer) == ArchiveStageResult::SealFailure);
    assert(stage.discarded);
  }
  {
    auto source = good; Stage stage; stage.corruptOnSeal = true;
    assert(exercise(source, stage, signer) == ArchiveStageResult::StageUntrusted);
    assert(stage.discarded);
  }
  {
    auto source = good; Stage stage; stage.mutateSourceOnBegin = true;
    assert(exercise(source, stage, signer) == ArchiveStageResult::StageUntrusted);
    assert(stage.discarded);
  }
  {
    auto source = good; Stage stage; stage.swap = &alternate;
    assert(exercise(source, stage, signer) == ArchiveStageResult::DifferentPackage);
    assert(stage.discarded);
  }
  std::puts("Signed staging: source substitution, changed payload, policy, write, seal and verification failures passed");
}
