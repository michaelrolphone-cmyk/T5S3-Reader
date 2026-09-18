#include "runtime/packages/PackageOrdinaryStage.h"

#include <openssl/evp.h>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace RuntimePackages;
namespace {
struct Hash {
  EVP_MD_CTX* context = EVP_MD_CTX_new();
  ~Hash() { EVP_MD_CTX_free(context); }
  bool start() { return context && EVP_DigestInit_ex(context, EVP_sha256(), nullptr) == 1; }
  bool update(const uint8_t* bytes, size_t length) {
    return EVP_DigestUpdate(context, bytes, length) == 1;
  }
  bool finish(uint8_t output[32]) {
    unsigned count = 0;
    return EVP_DigestFinal_ex(context, output, &count) == 1 && count == 32;
  }
};

std::string hexSha(const std::vector<uint8_t>& payload) {
  Hash hash;
  uint8_t digest[32]{};
  assert(hash.start() && hash.update(payload.data(), payload.size()) &&
         hash.finish(digest));
  constexpr char alphabet[] = "0123456789abcdef";
  std::string hex(64, '0');
  for (size_t i = 0; i < 32; ++i) {
    hex[i * 2] = alphabet[digest[i] >> 4];
    hex[i * 2 + 1] = alphabet[digest[i] & 15];
  }
  return hex;
}

struct Source {
  std::map<std::string, std::vector<uint8_t>> files;
  bool failRead = false;
  bool entrySize(const char* name, uint64_t& size) {
    const auto it = files.find(name);
    if (it == files.end()) return false;
    size = it->second.size();
    return true;
  }
  bool readAt(const char* name, uint64_t at, uint8_t* output, size_t count) {
    const auto it = files.find(name);
    if (failRead || it == files.end() || !output || at > it->second.size() ||
        count > it->second.size() - static_cast<size_t>(at)) return false;
    std::memcpy(output, it->second.data() + at, count);
    return true;
  }
};
struct Directory : Source {
  std::vector<uint8_t> manifest;
  std::string active;
  bool started = false, sealed = false, discarded = false;
  bool failWrite = false, failBegin = false, corruptReadback = false, failSeal = false;
  bool begin(const OrdinaryPackagePlan&) {
    if (failBegin) return false; // No exclusive ownership was granted.
    started = true;
    return true;
  }
  bool beginEntry(const char* name, uint64_t) {
    active = name;
    return files.emplace(name, std::vector<uint8_t>{}).second;
  }
  bool append(const uint8_t* data, size_t length) {
    if (failWrite) return false;
    files.at(active).insert(files.at(active).end(), data, data + length);
    return true;
  }
  bool endEntry() {
    if (corruptReadback && !files.at(active).empty()) files.at(active)[0] ^= 1;
    return true;
  }
  bool readEntry(const char* name, uint64_t at, uint8_t* output, size_t count) {
    return readAt(name, at, output, count);
  }
  bool writeManifest(const uint8_t* data, size_t size) {
    manifest.assign(data, data + size);
    return true;
  }
  bool seal() { sealed = !failSeal; return sealed; }
  bool discard() {
    discarded = true;
    files.clear();
    manifest.clear();
    return true;
  }
  bool exactEntries(const OrdinaryPackagePlan& plan) {
    if (manifest.empty() || files.size() != plan.entryCount) return false;
    for (size_t i = 0; i < plan.entryCount; ++i)
      if (files.count(plan.entries[i].name) != 1) return false;
    return true;
  }
};

OrdinaryPackagePlan plan(Kind kind, const Source& source) {
  OrdinaryPackagePlan result{};
  assert(makeIdentity(kind, "test-module", "1.2.3", "module.elf", false,
                      &result.identity));
  std::strcpy(result.architecture, "xtensa-esp32s3");
  result.minRuntimeApi = 2;
  result.entryCount = 2;
  std::strcpy(result.entries[0].name, "module.elf");
  std::strcpy(result.entries[1].name, "schema.json");
  result.entries[0].executable = true;
  for (size_t i = 0; i < 2; ++i) {
    const auto& bytes = source.files.at(result.entries[i].name);
    result.entries[i].sizeBytes = bytes.size();
    const std::string digest = hexSha(bytes);
    std::memcpy(result.entries[i].sha256, digest.c_str(), digest.size() + 1);
  }
  result.requirementCount = 1;
  std::strcpy(result.requirements[0].capability, "kernel.serial");
  result.requirements[0].minApi = 1;
  return result;
}

Source fixture() {
  Source out;
  out.files["module.elf"] = std::vector<uint8_t>(96, 0);
  auto& elf = out.files["module.elf"];
  std::memcpy(elf.data(), "\x7f" "ELF\x01\x01", 6);
  elf[6] = 1;
  elf[16] = 3;
  elf[18] = 94;
  elf[20] = 1;
  out.files["schema.json"] = std::vector<uint8_t>(1200, 0x7b);
  return out;
}
constexpr uint8_t metadata[] = "{\"type\":\"ordinary\"}";
constexpr PackageRuntimePolicy kLimits{"xtensa-esp32s3", 2, 0, 2048, 4096};
uint32_t resolve(const char* name) {
  return std::strcmp(name, "kernel.serial") == 0 ? 1u : 0u;
}

OrdinaryStageResult stage(const OrdinaryPackagePlan& candidate, Source& source,
                          Directory& target, PackageRuntimePolicy policy = kLimits) {
  Hash sha;
  uint8_t io[kOrdinaryIoBytes]{};
  return stageOrdinaryPackage(candidate, metadata, sizeof(metadata) - 1,
      source, target, sha, resolve, policy, io);
}
void validKindsAndSources() {
  for (Kind kind : {Kind::Application, Kind::Driver, Kind::Service, Kind::Provider}) {
    for (int transport = 0; transport < 2; ++transport) {
      Source source = fixture(); // Identical SD and downloaded input bytes.
      const auto candidate = plan(kind, source);
      Directory installed;
      assert(stage(candidate, source, installed) ==
             OrdinaryStageResult::ReadyForPublicationReview);
      assert(installed.sealed && !installed.discarded);
      assert(installed.manifest.size() == sizeof(metadata) - 1);
      Hash sha;
      uint8_t io[kOrdinaryIoBytes]{};
      assert(verifyOrdinaryDirectory(candidate, installed, sha, resolve, kLimits, io));
      installed.files["extra.dat"] = {1};
      assert(!verifyOrdinaryDirectory(candidate, installed, sha, resolve, kLimits, io));
      installed.files.erase("extra.dat");
      installed.files["module.elf"][25] ^= 1;
      assert(!verifyOrdinaryDirectory(candidate, installed, sha, resolve, kLimits, io));
      installed.files["module.elf"][25] ^= 1;
      assert(verifyOrdinaryDirectory(candidate, installed, sha, resolve, kLimits, io));
    }
  }
}
void failures() {
  Source source = fixture();
  auto candidate = plan(Kind::Driver, source);
  Directory target;
  candidate.entries[0].sha256[0] = candidate.entries[0].sha256[0] == '0' ? '1' : '0';
  assert(stage(candidate, source, target) == OrdinaryStageResult::IntegrityMismatch);
  assert(target.discarded && target.files.empty());
  candidate = plan(Kind::Driver, source);
  source.failRead = true;
  Directory missing;
  assert(stage(candidate, source, missing) == OrdinaryStageResult::ReadFailure);
  source.failRead = false;
  Directory corrupt;
  corrupt.corruptReadback = true;
  assert(stage(candidate, source, corrupt) == OrdinaryStageResult::ReadbackFailure);
  assert(corrupt.files.empty());
  Directory failWrite;
  failWrite.failWrite = true;
  assert(stage(candidate, source, failWrite) == OrdinaryStageResult::WriteFailure);
  Directory failSeal;
  failSeal.failSeal = true;
  assert(stage(candidate, source, failSeal) == OrdinaryStageResult::SealFailure);
  Directory ignored;
  candidate.architecture[0] = 'x';
  candidate.architecture[1] = '\0';
  assert(stage(candidate, source, ignored) == OrdinaryStageResult::PreflightRejected);
  assert(!ignored.started);
  candidate = plan(Kind::Driver, source);
  candidate.requirements[0].minApi = 3;
  assert(stage(candidate, source, ignored) == OrdinaryStageResult::PreflightRejected);
  candidate = plan(Kind::Driver, source);
  source.files["module.elf"].push_back(0);
  assert(stage(candidate, source, ignored) == OrdinaryStageResult::InvalidSourceSize);

  // A failed exclusive begin cannot authorize deleting someone else's stage.
  source = fixture();
  candidate = plan(Kind::Driver, source);
  Directory preexisting;
  preexisting.failBegin = true;
  preexisting.files["unmanaged-user-file"] = {0xa5, 0x5a};
  preexisting.manifest = {0x99};
  assert(stage(candidate, source, preexisting) == OrdinaryStageResult::StageUnavailable);
  assert(!preexisting.discarded && !preexisting.started);
  assert(preexisting.files.at("unmanaged-user-file") == std::vector<uint8_t>({0xa5, 0x5a}));
  assert(preexisting.manifest == std::vector<uint8_t>({0x99}));

  // The executable must have an entire ELF32 header, not just magic bytes.
  source.files["module.elf"].resize(20);
  candidate = plan(Kind::Driver, source);
  Directory shortElf;
  assert(stage(candidate, source, shortElf) == OrdinaryStageResult::PreflightRejected);
  assert(!shortElf.started);
  source = fixture();
  source.files["module.elf"][6] = 0;
  candidate = plan(Kind::Driver, source);
  Directory badVersion;
  assert(stage(candidate, source, badVersion) == OrdinaryStageResult::BadElf);
  assert(badVersion.discarded && badVersion.files.empty());
}
} // namespace

int main() {
  validKindsAndSources();
  failures();
  std::puts("Ordinary packages: four kinds, exclusive stage ownership, complete ELF32 header, SHA-256 and failure paths passed");
}
