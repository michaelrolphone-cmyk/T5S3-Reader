#include "PackageDeviceExtract.h"
#include "PackageDeviceSecurityFloor.h"
#include "PackageSignedProvenance.h"

#include <HalStorage.h>
#include <esp_task_wdt.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace RuntimePackages {
namespace {
constexpr const char* kExtractionLock = "/Packages/.extract.lock";
constexpr const char* kExtractProvenance = "/Packages/.extract.part/.risc-auth";

// Only an exclusively created lock, metadata file and this operation's entry
// files may be removed. An interrupted stage is deliberately NOT overwritten.
class HalExtractDirectory {
 public:
  bool begin(const PackageArchive& archive) {
    if (!Storage.ready() || !archive.entryCount ||
        archive.entryCount > kMaxPackageEntries ||
        !Storage.exists("/Packages") || Storage.exists(kPackageExtractStage))
      return false;
    lock_ = Storage.open(kExtractionLock,
        static_cast<oflag_t>(O_CREAT | O_EXCL | O_RDWR));
    if (!lock_.isOpen() || lock_.isDirectory()) return false;
    ownsLock_ = true;
    // This lock serializes cooperating installers, not physical SD edits.
    if (Storage.exists(kPackageExtractStage) ||
        !Storage.mkdir(kPackageExtractStage, false)) return false;
    ownsDirectory_ = true;
    count_ = archive.entryCount;
    for (size_t i = 0; i < count_; ++i) {
      if (!safePackageEntryName(archive.entries[i].name)) return false;
      std::memcpy(names_[i], archive.entries[i].name,
                  std::strlen(archive.entries[i].name) + 1);
    }
    return true;
  }

  bool beginEntry(const char* name, uint64_t bytes) {
    if (!ownsDirectory_ || !name || next_ >= count_ ||
        std::strcmp(name, names_[next_]) || !bytes || writer_.isOpen())
      return false;
    if (reader_.isOpen() && !reader_.close()) return false;
    if (!makePath(name, path_) || Storage.exists(path_)) return false;
    writer_ = Storage.open(path_, static_cast<oflag_t>(O_CREAT | O_EXCL | O_RDWR));
    if (!writer_.isOpen() || writer_.isDirectory()) return false;
    ownsFile_[next_] = true;
    expected_ = bytes;
    written_ = 0;
    std::memcpy(active_, name, std::strlen(name) + 1);
    return true;
  }

  bool append(const uint8_t* data, size_t bytes) {
    if (!writer_.isOpen() || !data || !bytes || written_ > expected_ ||
        bytes > expected_ - written_) return false;
    if (writer_.write(data, bytes) != bytes) return false;
    written_ += bytes;
    if ((written_ & 0x3fffu) < bytes) (void)esp_task_wdt_reset();
    return true;
  }

  bool endEntry() {
    if (!writer_.isOpen() || written_ != expected_) return false;
    writer_.flush();
    if (!writer_.close()) return false;
    reader_ = Storage.open(path_, O_RDONLY);
    if (!reader_.isOpen() || reader_.isDirectory() ||
        reader_.fileSize64() != expected_) return false;
    ++next_;
    return true;
  }

  bool readEntry(const char* name, uint64_t at, uint8_t* output, size_t bytes) {
    if (!reader_.isOpen() || !name || std::strcmp(name, active_) || !output ||
        at > expected_ || bytes > expected_ - at || !reader_.seek64(at))
      return false;
    return reader_.read(output, bytes) == static_cast<int>(bytes);
  }

  bool writeProvenance(const uint8_t* prefix, size_t prefixLength,
                       const uint8_t* signature, size_t signatureLength) {
    if (!ownsDirectory_ || next_ != count_ || !prefix || !signature ||
        prefixLength < kPackageHeaderBytes + 16 ||
        prefixLength > kPackageHeaderBytes + kPackageManifestLimit ||
        signatureLength != kPackageSignatureBytes ||
        writer_.isOpen() || ownsProvenance_ || Storage.exists(kExtractProvenance))
      return false;
    if (reader_.isOpen() && !reader_.close()) return false;
    HalFile metadata = Storage.open(kExtractProvenance,
        static_cast<oflag_t>(O_CREAT | O_EXCL | O_RDWR));
    if (!metadata.isOpen() || metadata.isDirectory()) return false;
    ownsProvenance_ = true;
    const bool written = metadata.write(prefix, prefixLength) == prefixLength &&
                         metadata.write(signature, signatureLength) == signatureLength;
    metadata.flush();
    if (!metadata.close() || !written) return false;
    HalFile check = Storage.open(kExtractProvenance, O_RDONLY);
    if (!check.isOpen() || check.isDirectory() ||
        check.fileSize64() != prefixLength + signatureLength) return false;
    uint8_t scratch[256]{};
    for (size_t pos = 0; pos < prefixLength;) {
      const size_t count = prefixLength - pos < sizeof(scratch) ?
          prefixLength - pos : sizeof(scratch);
      if (check.read(scratch, count) != static_cast<int>(count) ||
          std::memcmp(scratch, prefix + pos, count)) return false;
      pos += count;
    }
    if (check.read(scratch, signatureLength) != static_cast<int>(signatureLength) ||
        std::memcmp(scratch, signature, signatureLength)) return false;
    return check.close();
  }

  bool seal() {
    if (!ownsDirectory_ || next_ != count_ || writer_.isOpen() ||
        !ownsProvenance_) return false;
    if (reader_.isOpen() && !reader_.close()) return false;
    // Do NOT release the cooperative installer lock until AFTER the generic
    // extractor's final intake reauthentication and floor recheck. Destructor
    // releases it upon return; an error can still discard under the lock.
    return true;
  }

  bool discard() {
    if (reader_.isOpen()) (void)reader_.close();
    if (writer_.isOpen()) (void)writer_.close();
    bool clean = true;
    if (ownsDirectory_) {
      if (ownsProvenance_) {
        if (Storage.remove(kExtractProvenance)) ownsProvenance_ = false;
        else clean = false;
      }
      for (size_t i = 0; i < count_; ++i) {
        if (!ownsFile_[i]) continue;
        char path[160]{};
        if (!makePath(names_[i], path) || !Storage.remove(path)) clean = false;
        else ownsFile_[i] = false;
      }
      // Unknown additions cause rmdir to fail, preserving them for review.
      if (clean && Storage.rmdir(kPackageExtractStage)) ownsDirectory_ = false;
      else clean = false;
    }
    return releaseLock() && clean;
  }

  ~HalExtractDirectory() {
    if (reader_.isOpen()) (void)reader_.close();
    if (writer_.isOpen()) (void)writer_.close();
    (void)releaseLock();
    // Success leaves the complete extracted generation for publication review.
  }

 private:
  static bool makePath(const char* name, char (&path)[160]) {
    if (!safePackageEntryName(name)) return false;
    const int length = std::snprintf(path, sizeof(path), "%s/%s",
                                     kPackageExtractStage, name);
    return length > 0 && static_cast<size_t>(length) < sizeof(path);
  }
  bool releaseLock() {
    if (lock_.isOpen() && !lock_.close()) return false;
    if (!ownsLock_) return true;
    if (!Storage.remove(kExtractionLock)) return false;
    ownsLock_ = false;
    return true;
  }

  HalFile lock_{};
  HalFile writer_{};
  HalFile reader_{};
  char names_[kMaxPackageEntries][128]{};
  bool ownsFile_[kMaxPackageEntries]{};
  char path_[160]{};
  char active_[128]{};
  size_t count_ = 0;
  size_t next_ = 0;
  uint64_t expected_ = 0;
  uint64_t written_ = 0;
  bool ownsLock_ = false;
  bool ownsDirectory_ = false;
  bool ownsProvenance_ = false;
};
} // namespace

ArchiveExtractResult extractSignedDevicePackage(
    const uint8_t expectedSignedPrefixDigest[32],
    const TrustedPackageSigner* signers, size_t signerCount,
    const PackageRuntimePolicy& policy, PackageCapabilityApi resolveCapability,
    void* resolverContext, PackageVerificationWorkspace& workspace,
    PackageArchive& result, PackageArchiveLimits limits, bool allowFirstInstall) {
  result = {};
  constexpr uint64_t overhead = kPackageHeaderBytes + kPackageManifestLimit +
                                kPackageSignatureBytes;
  if (!expectedSignedPrefixDigest || !signers || !signerCount ||
      signerCount > 16 || !policy.architecture || !policy.runtimeApi ||
      !limits.maxTotalBytes ||
      limits.maxTotalBytes > std::numeric_limits<uint64_t>::max() - overhead ||
      !Storage.ready()) return ArchiveExtractResult::InvalidInput;

  HalFile intake = Storage.open(kPackageIntakeStage, O_RDONLY);
  if (!intake.isOpen() || intake.isDirectory()) return ArchiveExtractResult::InvalidInput;
  const uint64_t length = intake.fileSize64();
  if (length < kPackageHeaderBytes || length > limits.maxTotalBytes + overhead)
    return ArchiveExtractResult::InvalidInput;
  auto readIntake = [&intake, length](uint64_t at, uint8_t* output, size_t bytes) {
    if (!output || at > length || bytes > length - at ||
        !intake.seek64(at)) return false;
    const bool read = intake.read(output, bytes) == static_cast<int>(bytes);
    if (read && (at & 0x3fffu) < bytes) (void)esp_task_wdt_reset();
    return read;
  };

  PackageMbedtlsSha256 hash;
  PackageDeviceTrustVerifier verifier(result, signers, signerCount);
  HalExtractDirectory stage;
  return extractSignedPackageArchive(readIntake, length,
      expectedSignedPrefixDigest, stage, hash, verifier,
      [resolveCapability, resolverContext](const char* capability) -> uint32_t {
        return resolveCapability ? resolveCapability(capability, resolverContext) : 0;
      },
      [allowFirstInstall](const PackageArchive& candidate) {
        return checkPackageSecurityFloor(devicePackageSecurityFloors(), candidate,
                                         allowFirstInstall) == FloorCheck::Allowed;
      }, policy, result, workspace, limits);
}

} // namespace RuntimePackages
