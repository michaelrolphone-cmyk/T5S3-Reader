// Reuse the real OpenSSL SHA-256/P-256 fixture verifier from the staging test;
// compile this as a separate executable so both original test mains run.
#define main original_stage_main
#include "package_archive_stage_test.cpp"
#undef main
#include "runtime/packages/PackageArchiveExtract.h"

#include <map>
#include <string>

namespace {
struct ExtractDirectory {
  std::map<std::string, std::vector<uint8_t>> files;
  std::string active;
  std::vector<uint8_t>* source = nullptr;
  const std::vector<uint8_t>* replacement = nullptr;
  bool failBegin = false, failAppend = false, failRead = false;
  bool corruptOutput = false, failSeal = false, mutateSourceAtSeal = false;
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
    if (failAppend && ++appends == 2) return false;
    if (!failAppend) ++appends;
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
  bool seal() {
    if (failSeal) return false;
    sealed = true;
    if (mutateSourceAtSeal) source->back() ^= 1;
    return true;
  }
  bool discard() { discarded = true; sealed = false; files.clear(); return true; }
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
               !(rejectFinalFloor && floorChecks == 2);
      }, policy, archive, workspace);
  if (result == ArchiveExtractResult::ReadyForPublicationReview) {
    assert(directory.sealed && !directory.discarded && floorChecks == 2);
    assert(archive.entryCount == directory.files.size());
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
    assert(directory.files.empty());
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
  std::puts("Signed extraction: independent readback, intake identity, floor and fault cases passed");
}
