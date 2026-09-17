// Reuse the real OpenSSL SHA-256/P-256 fixture verifier from staging.
#define main original_stage_main
#include "package_archive_stage_test.cpp"
#undef main
#include "runtime/packages/PackageArchiveExtract.h"
#include "runtime/packages/PackageSignedProvenance.h"

#include <map>
#include <string>

namespace {
struct ExtractDirectory {
  std::map<std::string, std::vector<uint8_t>> files;
  std::vector<uint8_t> provenance;
  std::string active;
  std::vector<uint8_t>* source = nullptr;
  const std::vector<uint8_t>* replacement = nullptr;
  bool failBegin = false, failAppend = false, failRead = false;
  bool corruptOutput = false, failProvenance = false, failSeal = false;
  bool mutateSourceAtSeal = false;
  bool started = false, sealed = false, discarded = false;
  size_t appends = 0;

  bool begin(const PackageArchive&) {
    started = true;
    if (replacement) *source = *replacement;
    return !failBegin;
  }
  bool beginEntry(const char* name, uint64_t size) {
    if (!started || !name || size > 1024 * 1024) return false;
    active = name;
    files[active].clear();
    return true;
  }
  bool append(const uint8_t* bytes, size_t count) {
    ++appends;
    if (failAppend && appends == 2) return false;
    auto& output = files[active];
    output.insert(output.end(), bytes, bytes + count);
    return true;
  }
  bool endEntry() {
    if (corruptOutput && active == "driver.elf") files[active][25] ^= 1;
    active.clear();
    return true;
  }
  bool readEntry(const char* name, uint64_t offset, uint8_t* bytes, size_t count) {
    if (failRead) return false;
    const auto it = files.find(name);
    if (it == files.end() || offset > it->second.size() ||
        count > it->second.size() - static_cast<size_t>(offset)) return false;
    std::memcpy(bytes, it->second.data() + offset, count);
    return true;
  }
  bool writeProvenance(const uint8_t* prefix, size_t prefixLength,
                       const uint8_t* signature, size_t signatureLength) {
    if (failProvenance) return false;
    provenance.assign(prefix, prefix + prefixLength);
    provenance.insert(provenance.end(), signature, signature + signatureLength);
    return true;
  }
  bool seal() {
    if (failSeal) return false;
    sealed = true;
    if (mutateSourceAtSeal) source->back() ^= 1;
    return true;
  }
  bool discard() {
    discarded = true;
    sealed = false;
    files.clear();
    provenance.clear();
    return true;
  }
};

ArchiveExtractResult extract(std::vector<uint8_t>& source,
                             ExtractDirectory& directory, Signer& signer,
                             const uint8_t expected[32], bool rejectFinalFloor = false) {
  directory.source = &source;
  auto read = [&source](uint64_t at, uint8_t* output, size_t count) {
    if (at > source.size() || count > source.size() - static_cast<size_t>(at)) return false;
    std::memcpy(output, source.data() + at, count);
    return true;
  };
  Sha256 sha;
  static PackageVerificationWorkspace workspace{};
  static PackageArchive archive{};
  unsigned floorChecks = 0;
  const PackageRuntimePolicy policy{"xtensa-esp32s3", 2, 3,
                                    1024 * 1024, 4 * 1024 * 1024};
  const auto result = extractSignedPackageArchive(read, source.size(), expected,
      directory, sha, signer,
      [](const char* cap) -> uint32_t {
        return std::strcmp(cap, "kernel.serial") == 0 ? 1u : 0u;
      },
      [&floorChecks, rejectFinalFloor](const PackageArchive& candidate) {
        ++floorChecks;
        return candidate.securityVersion >= 3 &&
               !(rejectFinalFloor && floorChecks >= 2);
      }, policy, archive, workspace);
  if (result == ArchiveExtractResult::ReadyForPublicationReview) {
    assert(directory.sealed && !directory.discarded && floorChecks == 3);
    assert(archive.entryCount == directory.files.size());
    assert(directory.provenance.size() == static_cast<size_t>(archive.signatureOffset) +
                                          kPackageSignatureBytes);
    assert(std::memcmp(directory.provenance.data(), source.data(),
                       directory.provenance.size()) == 0);
    for (size_t i = 0; i < archive.entryCount; ++i) {
      const auto& entry = archive.entries[i];
      const auto& file = directory.files.at(entry.name);
      assert(file.size() == entry.sizeBytes);
      assert(std::memcmp(file.data(), source.data() + entry.offset,
                         file.size()) == 0);
    }
  } else {
    assert(!archive.identity.id[0]);
    assert(!directory.started || directory.discarded);
    assert(directory.files.empty() && directory.provenance.empty());
  }
  return result;
}

ProvenanceResult verifyDirectory(ExtractDirectory& directory, Signer& signer,
                                 const uint8_t* expected = nullptr,
                                 bool floorAllowed = true) {
  Sha256 sha;
  static PackageVerificationWorkspace workspace{};
  static PackageArchive archive{};
  const PackageRuntimePolicy policy{"xtensa-esp32s3", 2, 3,
                                    1024 * 1024, 4 * 1024 * 1024};
  const auto readProvenance = [&directory](uint64_t at, uint8_t* output, size_t count) {
    if (at > directory.provenance.size() ||
        count > directory.provenance.size() - static_cast<size_t>(at)) return false;
    std::memcpy(output, directory.provenance.data() + at, count);
    return true;
  };
  const auto size = [&directory](const char* name, uint64_t& result) {
    const auto it = directory.files.find(name);
    if (it == directory.files.end()) return false;
    result = it->second.size();
    return true;
  };
  const auto read = [&directory](const char* name, uint64_t at, uint8_t* output,
                                  size_t count) {
    const auto it = directory.files.find(name);
    if (it == directory.files.end() || at > it->second.size() ||
        count > it->second.size() - static_cast<size_t>(at)) return false;
    std::memcpy(output, it->second.data() + at, count);
    return true;
  };
  const auto exact = [&directory](const PackageArchive& candidate) {
    if (directory.files.size() != candidate.entryCount) return false;
    for (size_t i = 0; i < candidate.entryCount; ++i)
      if (directory.files.count(candidate.entries[i].name) != 1) return false;
    return true;
  };
  return verifySignedPackageDirectory(readProvenance, directory.provenance.size(),
      size, read, exact, sha, signer,
      [](const char* cap) -> uint32_t {
        return std::strcmp(cap, "kernel.serial") == 0 ? 1u : 0u;
      },
      [floorAllowed](const PackageArchive& candidate) {
        return floorAllowed && candidate.securityVersion >= 3;
      }, policy, archive, workspace, expected);
}
} // namespace

int main(int argc, char** argv) {
  assert(argc == 4);
  const auto good = load(argv[1]);
  const auto alternate = load(argv[2]);
  assert(good.size() == alternate.size() && good != alternate);
  Signer signer(argv[3]);
  auto source = good;
  auto read = [&source](uint64_t at, uint8_t* bytes, size_t count) {
    if (at > source.size() || count > source.size() - static_cast<size_t>(at)) return false;
    std::memcpy(bytes, source.data() + at, count);
    return true;
  };
  Sha256 sha;
  static PackageVerificationWorkspace workspace{};
  static PackageArchive archive{};
  assert(verifyPackageArchive(read, source.size(), archive, workspace, sha, signer) ==
         PackageVerifyResult::AuthenticatedContent);
  uint8_t expected[32]{};
  assert(sha.start() && sha.update(workspace.signedPrefix,
                                  static_cast<size_t>(archive.signatureOffset)) &&
         sha.finish(expected));
  {
    ExtractDirectory directory;
    assert(extract(source, directory, signer, expected) ==
           ArchiveExtractResult::ReadyForPublicationReview);
    assert(verifyDirectory(directory, signer, expected) ==
           ProvenanceResult::AuthenticatedDirectory);
    assert(verifyDirectory(directory, signer) ==
           ProvenanceResult::AuthenticatedDirectory); // Restart: no RAM digest.
    directory.files["driver.elf"][25] ^= 1;
    assert(verifyDirectory(directory, signer) == ProvenanceResult::EntryCorrupt);
    directory.files["driver.elf"][25] ^= 1;
    directory.provenance.back() ^= 1;
    assert(verifyDirectory(directory, signer) == ProvenanceResult::SignatureRejected);
    directory.provenance.back() ^= 1;
    directory.files["unknown"] = {1};
    assert(verifyDirectory(directory, signer) == ProvenanceResult::UnexpectedFiles);
    directory.files.erase("unknown");
    directory.files.erase("schema.json");
    assert(verifyDirectory(directory, signer) == ProvenanceResult::UnexpectedFiles);
  }
  {
    source = alternate; ExtractDirectory directory;
    assert(extract(source, directory, signer, expected) ==
           ArchiveExtractResult::DifferentPackage);
    assert(!directory.started);
  }
  {
    source = good; source.back() ^= 1; ExtractDirectory directory;
    assert(extract(source, directory, signer, expected) ==
           ArchiveExtractResult::IntakeUntrusted);
  }
  {
    source = good; ExtractDirectory directory; directory.replacement = &alternate;
    assert(extract(source, directory, signer, expected) ==
           ArchiveExtractResult::IntakeChanged);
  }
  {
    source = good; ExtractDirectory directory; directory.failBegin = true;
    assert(extract(source, directory, signer, expected) ==
           ArchiveExtractResult::StageUnavailable);
  }
  {
    source = good; ExtractDirectory directory; directory.failAppend = true;
    assert(extract(source, directory, signer, expected) ==
           ArchiveExtractResult::CopyFailure);
  }
  {
    source = good; ExtractDirectory directory; directory.corruptOutput = true;
    assert(extract(source, directory, signer, expected) ==
           ArchiveExtractResult::ReadbackFailure);
  }
  {
    source = good; ExtractDirectory directory; directory.failRead = true;
    assert(extract(source, directory, signer, expected) ==
           ArchiveExtractResult::ReadbackFailure);
  }
  {
    source = good; ExtractDirectory directory; directory.failProvenance = true;
    assert(extract(source, directory, signer, expected) ==
           ArchiveExtractResult::ProvenanceFailure);
  }
  {
    source = good; ExtractDirectory directory; directory.failSeal = true;
    assert(extract(source, directory, signer, expected) ==
           ArchiveExtractResult::SealFailure);
  }
  {
    source = good; ExtractDirectory directory; directory.mutateSourceAtSeal = true;
    assert(extract(source, directory, signer, expected) ==
           ArchiveExtractResult::IntakeChanged);
  }
  {
    source = good; ExtractDirectory directory;
    assert(extract(source, directory, signer, expected, true) ==
           ArchiveExtractResult::FloorRejected);
  }
  {
    source = good; ExtractDirectory directory;
    assert(extract(source, directory, signer, expected) ==
           ArchiveExtractResult::ReadyForPublicationReview);
    assert(verifyDirectory(directory, signer, expected, false) ==
           ProvenanceResult::FloorRejected);
    directory.provenance[20] ^= 1;
    assert(verifyDirectory(directory, signer, expected) !=
           ProvenanceResult::AuthenticatedDirectory);
  }
  std::puts("Signed extraction/provenance: P-256, readback, restart, identity, floor and fault cases passed");
  return 0;
}
