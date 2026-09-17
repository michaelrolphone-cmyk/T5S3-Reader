#include "runtime/packages/PackageOrdinaryInstaller.h"

#include <openssl/evp.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace RuntimePackages;
namespace {
constexpr TransactionPaths kPaths{"/Drivers/test", "/Drivers/.test.install", "/Drivers/.test.previous"};
constexpr PackageRuntimePolicy kPolicy{"xtensa-esp32s3", 2, 0, 4096, 8192};
constexpr uint8_t kManifest[] = "{\"kind\":\"driver\",\"id\":\"test\"}";
struct Hash {
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  ~Hash() { EVP_MD_CTX_free(ctx); }
  bool start() { return ctx && EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1; }
  bool update(const uint8_t* data, size_t size) {
    return EVP_DigestUpdate(ctx, data, size) == 1;
  }
  bool finish(uint8_t digest[32]) {
    unsigned count = 0;
    return EVP_DigestFinal_ex(ctx, digest, &count) == 1 && count == 32;
  }
};
std::string digest(const std::vector<uint8_t>& data) {
  Hash h;
  uint8_t bytes[32]{};
  assert(h.start() && h.update(data.data(), data.size()) && h.finish(bytes));
  constexpr char hex[] = "0123456789abcdef";
  std::string result(64, '0');
  for (size_t i = 0; i < 32; ++i) {
    result[2 * i] = hex[bytes[i] >> 4];
    result[2 * i + 1] = hex[bytes[i] & 15];
  }
  return result;
}
struct Source {
  std::map<std::string, std::vector<uint8_t>> files;
  bool badRead = false;
  bool entrySize(const char* name, uint64_t& length) const {
    const auto it = files.find(name);
    if (it == files.end()) return false;
    length = it->second.size();
    return true;
  }
  bool readAt(const char* name, uint64_t offset, uint8_t* data, size_t size) const {
    const auto it = files.find(name);
    if (badRead || it == files.end() || offset > it->second.size() ||
        size > it->second.size() - static_cast<size_t>(offset)) return false;
    std::memcpy(data, it->second.data() + offset, size);
    return true;
  }
};
struct Storage {
  // The real adapter MUST parse the directory's own retained manifest and
  // hash its exact entries. This mock exercises publication ordering/faults;
  // package_ordinary_stage_test.cpp exercises actual SHA/readback verification.
  std::map<std::string, bool> directories;
  int renames = 0;
  int failRename = -1;
  bool failVerifyStage = false;
  bool failPurge = false;
  bool exists(const char* path) const { return directories.count(path) != 0; }
  bool rename(const char* from, const char* to) {
    if (++renames == failRename || !exists(from) || exists(to)) return false;
    directories[to] = directories.at(from);
    directories.erase(from);
    return true;
  }
  bool verify(const char* path) const {
    if (failVerifyStage && std::strcmp(path, kPaths.stage) == 0) return false;
    const auto it = directories.find(path);
    return it != directories.end() && it->second;
  }
  bool purge(const char* path) {
    if (failPurge || !verify(path)) return false;
    return directories.erase(path) == 1;
  }
};
struct Stage {
  Storage& storage;
  std::map<std::string, std::vector<uint8_t>> files;
  std::vector<uint8_t> manifest;
  std::string writing;
  bool ownsStage = false;
  bool begin(const OrdinaryPackagePlan&) {
    if (storage.exists(kPaths.stage)) return false;
    storage.directories[kPaths.stage] = false;
    ownsStage = true;
    return true;
  }
  bool beginEntry(const char* name, uint64_t) {
    writing = name;
    return files.emplace(name, std::vector<uint8_t>{}).second;
  }
  bool append(const uint8_t* data, size_t length) {
    files.at(writing).insert(files.at(writing).end(), data, data + length);
    return true;
  }
  bool endEntry() { return true; }
  bool readEntry(const char* name, uint64_t offset, uint8_t* data, size_t length) {
    auto it = files.find(name);
    if (it == files.end() || offset > it->second.size() ||
        length > it->second.size() - static_cast<size_t>(offset)) return false;
    std::memcpy(data, it->second.data() + offset, length);
    return true;
  }
  bool writeManifest(const uint8_t* bytes, size_t length) {
    manifest.assign(bytes, bytes + length);
    return true;
  }
  bool seal() {
    if (!ownsStage || manifest.empty()) return false;
    storage.directories[kPaths.stage] = true;
    return true;
  }
  bool discard() {
    if (ownsStage) storage.directories.erase(kPaths.stage);
    ownsStage = false;
    return true;
  }
};
Source makeSource() {
  Source source;
  auto& elf = source.files["module.elf"];
  elf.resize(96);
  std::memcpy(elf.data(), "\x7f" "ELF\x01\x01", 6);
  elf[16] = 3;
  elf[18] = 94;
  source.files["schema.json"] = std::vector<uint8_t>(1100, 0x42);
  return source;
}
OrdinaryPackagePlan makePlan(const Source& source, Kind kind) {
  OrdinaryPackagePlan plan{};
  assert(makeIdentity(kind, "test", "1.2.3", "module.elf", false, &plan.identity));
  std::strcpy(plan.architecture, "xtensa-esp32s3");
  plan.minRuntimeApi = 1;
  plan.entryCount = 2;
  for (size_t i = 0; i < plan.entryCount; ++i) {
    const char* name = i ? "schema.json" : "module.elf";
    auto& entry = plan.entries[i];
    std::strcpy(entry.name, name);
    entry.executable = i == 0;
    entry.sizeBytes = source.files.at(name).size();
    const std::string sha = digest(source.files.at(name));
    std::strcpy(entry.sha256, sha.c_str());
  }
  return plan;
}
uint32_t resolver(const char*) { return 0; }
OrdinaryInstallOutcome install(Source& source, Stage& stage, Storage& disk,
                               const OrdinaryPackagePlan& plan, bool allowed = true) {
  Hash hash;
  uint8_t io[kOrdinaryIoBytes]{};
  return installOrdinaryPackage(plan, kManifest, sizeof(kManifest) - 1,
      source, stage, hash, resolver, kPolicy, io, disk, kPaths,
      [&disk](const char* path) { return disk.verify(path); },
      [&disk](const char* path) { return disk.purge(path); }, allowed);
}
void successfulFourKindsAndSources() {
  for (Kind kind : {Kind::Application, Kind::Driver, Kind::Service, Kind::Provider})
    for (int sourceType = 0; sourceType != 2; ++sourceType) {
      (void)sourceType; // Both SD and downloader supply the same source contract.
      Source source = makeSource();
      auto plan = makePlan(source, kind);
      Storage disk;
      Stage stage{disk};
      const auto result = install(source, stage, disk, plan);
      assert(result.result == OrdinaryInstallResult::Installed);
      assert(result.staging == OrdinaryStageResult::ReadyForPublicationReview);
      assert(disk.verify(kPaths.target) && !disk.exists(kPaths.stage));
      assert(!disk.exists(kPaths.backup));
    }
}
void preservePriorAndRejectFailure() {
  Source source = makeSource();
  auto plan = makePlan(source, Kind::Driver);
  Storage disk;
  Stage stage{disk};
  disk.directories[kPaths.target] = true;
  disk.failRename = 2; // target -> backup succeeds, stage -> target fails.
  assert(install(source, stage, disk, plan).result ==
         OrdinaryInstallResult::PublicationRejected);
  assert(disk.verify(kPaths.target) || disk.verify(kPaths.backup));
  disk.failRename = -1;
  assert(install(source, stage, disk, plan).result ==
         OrdinaryInstallResult::StageAlreadyExists);
  // No caller is allowed to erase an interrupted stage automatically.
  assert(disk.exists(kPaths.stage));
}
void failWithoutModifyingExisting() {
  Source source = makeSource();
  auto plan = makePlan(source, Kind::Driver);
  Storage disk;
  Stage stage{disk};
  disk.directories[kPaths.target] = true;
  assert(install(source, stage, disk, plan, false).result ==
         OrdinaryInstallResult::InvalidInput);
  assert(!disk.exists(kPaths.stage));
  source.badRead = true;
  assert(install(source, stage, disk, plan).result ==
         OrdinaryInstallResult::StageRejected);
  assert(disk.verify(kPaths.target) && !disk.exists(kPaths.stage));
  source.badRead = false;
  disk.failVerifyStage = true;
  assert(install(source, stage, disk, plan).result ==
         OrdinaryInstallResult::StageVerificationRejected);
  assert(disk.verify(kPaths.target) && !disk.exists(kPaths.stage));
  disk.failVerifyStage = false;
  disk.directories[kPaths.stage] = true;
  assert(install(source, stage, disk, plan).result ==
         OrdinaryInstallResult::StageAlreadyExists);
  assert(disk.verify(kPaths.target) && disk.exists(kPaths.stage));
}
} // namespace
int main() {
  successfulFourKindsAndSources();
  preservePriorAndRejectFailure();
  failWithoutModifyingExisting();
  std::puts("Ordinary installer: unsigned four-kind staging, publication, backup recovery and failures PASS");
}
