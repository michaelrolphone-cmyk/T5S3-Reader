#include "runtime/packages/PackageArchive.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace RuntimePackages;
namespace {
void u16(std::vector<uint8_t>& out, uint16_t value) {
  out.push_back(static_cast<uint8_t>(value)); out.push_back(static_cast<uint8_t>(value >> 8));
}
void u32(std::vector<uint8_t>& out, uint32_t value) {
  u16(out, static_cast<uint16_t>(value)); u16(out, static_cast<uint16_t>(value >> 16));
}
void u64(std::vector<uint8_t>& out, uint64_t value) {
  u32(out, static_cast<uint32_t>(value)); u32(out, static_cast<uint32_t>(value >> 32));
}
void text(std::vector<uint8_t>& out, const char* value) {
  const size_t n = std::strlen(value);
  out.insert(out.end(), value, value + n);
}
struct Fixture {
  std::vector<uint8_t> bytes;
  size_t id = 0, version = 0, architecture = 0, firstEntry = 0, secondEntry = 0;
  size_t firstRequirement = 0, secondRequirement = 0;
  Fixture() {
    std::vector<uint8_t> manifest;
    const char* kindId = "gps-nmea";
    const char* ver = "1.2.3";
    const char* artifact = "driver.elf";
    const char* arch = "xtensa-esp32s3";
    manifest.insert(manifest.end(), {1, static_cast<uint8_t>(std::strlen(kindId)),
        static_cast<uint8_t>(std::strlen(ver)), static_cast<uint8_t>(std::strlen(artifact)),
        static_cast<uint8_t>(std::strlen(arch)), 0, 0, 0});
    u32(manifest, 2); u32(manifest, 3);
    id = kPackageHeaderBytes + manifest.size(); text(manifest, kindId);
    version = kPackageHeaderBytes + manifest.size(); text(manifest, ver);
    text(manifest, artifact);
    architecture = kPackageHeaderBytes + manifest.size(); text(manifest, arch);
    firstEntry = kPackageHeaderBytes + manifest.size();
    manifest.push_back(static_cast<uint8_t>(std::strlen(artifact))); manifest.push_back(1);
    u64(manifest, 64); manifest.insert(manifest.end(), 32, 0x41); text(manifest, artifact);
    secondEntry = kPackageHeaderBytes + manifest.size();
    manifest.push_back(11); manifest.push_back(0); u64(manifest, 16);
    manifest.insert(manifest.end(), 32, 0x42); text(manifest, "schema.json");
    firstRequirement = kPackageHeaderBytes + manifest.size();
    manifest.push_back(12); u32(manifest, 1); text(manifest, "kernel.clock");
    secondRequirement = kPackageHeaderBytes + manifest.size();
    manifest.push_back(13); u32(manifest, 1); text(manifest, "kernel.serial");
    text(bytes, "RISCPKG1"); u16(bytes, 1); u16(bytes, kPackageSignatureP256Sha256);
    u32(bytes, static_cast<uint32_t>(manifest.size())); u16(bytes, 2); u16(bytes, 2);
    u16(bytes, kPackageSignatureBytes); u16(bytes, 0); u64(bytes, 80); u32(bytes, 7);
    u64(bytes, static_cast<uint64_t>(kPackageHeaderBytes + manifest.size() + kPackageSignatureBytes + 80));
    u32(bytes, 0);
    assert(bytes.size() == kPackageHeaderBytes);
    bytes.insert(bytes.end(), manifest.begin(), manifest.end());
    bytes.insert(bytes.end(), kPackageSignatureBytes, 0x55);
    bytes.insert(bytes.end(), 80, 0xa5);
  }
  ArchiveResult decode(PackageArchive& out, PackageArchiveLimits limits = {}) const {
    uint8_t scratch[kPackageManifestLimit]{};
    return decodePackageArchive([this](uint64_t offset, uint8_t* dest, size_t length) {
      if (offset > bytes.size() || length > bytes.size() - static_cast<size_t>(offset)) return false;
      std::memcpy(dest, bytes.data() + offset, length);
      return true;
    }, bytes.size(), out, scratch, sizeof(scratch), limits);
  }
};
void acceptsCanonicalArchive() {
  Fixture f;
  PackageArchive out{};
  assert(f.decode(out) == ArchiveResult::ReadyForAuthentication);
  assert(out.identity.kind == Kind::Driver && std::strcmp(out.identity.id, "gps-nmea") == 0);
  assert(out.keyId == 7 && out.securityVersion == 3 && out.entryCount == 2);
  assert(out.signatureOffset + kPackageSignatureBytes == out.payloadOffset);
  assert(out.entries[0].offset == out.payloadOffset);
  assert(out.entries[1].offset == out.payloadOffset + 64);
  assert(std::strcmp(out.entries[0].sha256, "4141414141414141414141414141414141414141414141414141414141414141") == 0);
  const PackageRuntimePolicy policy{"xtensa-esp32s3", 2, 3, 1024, 4096};
  assert(preflightArchive(out, policy, [](const char*) -> uint32_t { return 1; }) ==
      PreflightResult::ReadyForContentVerification);
  const PackageRuntimePolicy badArch{"riscv32", 2, 3, 1024, 4096};
  assert(preflightArchive(out, badArch, [](const char*) -> uint32_t { return 1; }) ==
      PreflightResult::UnsupportedArchitecture);
  assert(preflightArchive(out, policy, [](const char*) -> uint32_t { return 0; }) ==
      PreflightResult::UnavailableCapability);
}
void headerAndSizeRejections() {
  PackageArchive out{};
  for (size_t position : {size_t(0), size_t(8), size_t(10), size_t(20), size_t(22), size_t(36), size_t(44)}) {
    Fixture f; f.bytes[position] ^= 1;
    assert(f.decode(out) != ArchiveResult::ReadyForAuthentication);
  }
  Fixture noKey; noKey.bytes[32] = 0;
  assert(noKey.decode(out) == ArchiveResult::InvalidLength);
  Fixture truncated; truncated.bytes.pop_back();
  assert(truncated.decode(out) == ArchiveResult::InvalidLength);
  Fixture appended; appended.bytes.push_back(0);
  assert(appended.decode(out) == ArchiveResult::InvalidLength);
  Fixture oversized; oversized.bytes[24] = 255;
  assert(oversized.decode(out) == ArchiveResult::InvalidLength);
  Fixture count; count.bytes[16] = 17;
  assert(count.decode(out) == ArchiveResult::InvalidLength);
  Fixture needs; needs.bytes[18] = 17;
  assert(needs.decode(out) == ArchiveResult::InvalidLength);
  Fixture smallBudget;
  assert(smallBudget.decode(out, {32, 4096}) == ArchiveResult::InvalidEntry);
  Fixture smallTotal;
  assert(smallTotal.decode(out, {1024, 64}) == ArchiveResult::InvalidLength);
  Fixture shortScratch;
  uint8_t scratch[16]{};
  assert(decodePackageArchive([&shortScratch](uint64_t offset, uint8_t* dest, size_t count) {
        if (offset > shortScratch.bytes.size() || count > shortScratch.bytes.size() - offset) return false;
        std::memcpy(dest, shortScratch.bytes.data() + offset, count); return true;
      }, shortScratch.bytes.size(), out, scratch, sizeof(scratch)) == ArchiveResult::InvalidLength);
  Fixture missing;
  assert(decodePackageArchive([](uint64_t, uint8_t*, size_t) { return false; },
      missing.bytes.size(), out, scratch, kPackageManifestLimit) == ArchiveResult::ReadFailure);
}
void metadataRejections() {
  PackageArchive out{};
  Fixture badKind; badKind.bytes[kPackageHeaderBytes] = 4;
  assert(badKind.decode(out) == ArchiveResult::InvalidMetadata);
  Fixture badReserved; badReserved.bytes[kPackageHeaderBytes + 5] = 1;
  assert(badReserved.decode(out) == ArchiveResult::InvalidMetadata);
  Fixture uppercase; uppercase.bytes[uppercase.id] = 'G';
  assert(uppercase.decode(out) == ArchiveResult::InvalidMetadata);
  Fixture badVersion; badVersion.bytes[badVersion.version] = 'x';
  assert(badVersion.decode(out) == ArchiveResult::InvalidMetadata);
  Fixture arch; arch.bytes[arch.architecture] = '/';
  assert(arch.decode(out) == ArchiveResult::InvalidMetadata);
  Fixture trailing; trailing.bytes[trailing.firstRequirement + 5] = 'k';
  trailing.bytes[12] -= 1;
  assert(trailing.decode(out) != ArchiveResult::ReadyForAuthentication);
}
void entryAndRequirementRejections() {
  PackageArchive out{};
  Fixture hidden; hidden.bytes[hidden.secondEntry + 42] = '.';
  assert(hidden.decode(out) == ArchiveResult::InvalidEntry);
  Fixture unsafe; unsafe.bytes[unsafe.firstEntry + 42] = '/';
  assert(unsafe.decode(out) == ArchiveResult::InvalidEntry);
  Fixture duplicate; duplicate.bytes[duplicate.secondEntry] = 10;
  std::memcpy(duplicate.bytes.data() + duplicate.secondEntry + 42, "driver.elf", 10);
  duplicate.bytes.erase(duplicate.bytes.begin() + duplicate.secondEntry + 52);
  --duplicate.bytes[12]; --duplicate.bytes[36];
  assert(duplicate.decode(out) == ArchiveResult::InvalidEntry);
  Fixture extraElf; std::memcpy(extraElf.bytes.data() + extraElf.secondEntry + 42,
      "module.elfx", 11);
  assert(extraElf.decode(out) == ArchiveResult::ReadyForAuthentication);
  Fixture badFlag; badFlag.bytes[badFlag.firstEntry + 1] = 2;
  assert(badFlag.decode(out) == ArchiveResult::InvalidEntry);
  Fixture badSize; badSize.bytes[badSize.firstEntry + 2] = 0;
  assert(badSize.decode(out) == ArchiveResult::InvalidEntry);
  Fixture missingExecutable; missingExecutable.bytes[missingExecutable.firstEntry + 1] = 0;
  assert(missingExecutable.decode(out) == ArchiveResult::ReadyForAuthentication);
  const PackageRuntimePolicy policy{"xtensa-esp32s3", 2, 3, 1024, 4096};
  assert(preflightArchive(out, policy, [](const char*) -> uint32_t { return 1; }) ==
      PreflightResult::InvalidEntry);
  Fixture requirement; requirement.bytes[requirement.firstRequirement + 5] = '/';
  assert(requirement.decode(out) == ArchiveResult::InvalidRequirement);
  Fixture unsorted; unsorted.bytes[unsorted.secondRequirement + 5] = 'a';
  assert(unsorted.decode(out) == ArchiveResult::InvalidRequirement);
  Fixture noApi; noApi.bytes[noApi.firstRequirement + 1] = 0;
  assert(noApi.decode(out) == ArchiveResult::InvalidRequirement);
}
} // namespace
int main() {
  acceptsCanonicalArchive();
  headerAndSizeRejections();
  metadataRejections();
  entryAndRequirementRejections();
  std::puts("Package archive: canonical framing, bounded metadata, sorted entries and fail-closed preflight passed");
}
