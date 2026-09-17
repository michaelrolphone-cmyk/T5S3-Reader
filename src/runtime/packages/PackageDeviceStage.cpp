#include "PackageDeviceStage.h"
#include "PackageDeviceSecurityFloor.h"

#include <HalStorage.h>
#include <esp_task_wdt.h>

#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace RuntimePackages {
namespace {

// No app-controlled paths, no O_TRUNC, and no removal of a stage left by an
// earlier reset. The separate recovery layer must explicitly dispose of it.
class HalArchiveStage {
 public:
  bool begin(uint64_t length) {
    if (!length || !Storage.ready()) return false;
    if (Storage.exists("/Packages")) {
      HalFile directory = Storage.open("/Packages", O_RDONLY);
      const bool valid = directory.isOpen() && directory.isDirectory();
      if (directory.isOpen()) (void)directory.close();
      if (!valid) return false;
    } else if (!Storage.mkdir("/Packages")) {
      return false;
    }
    if (Storage.exists(kPackageIntakeStage)) return false;
    writer_ = Storage.open(kPackageIntakeStage,
        static_cast<oflag_t>(O_CREAT | O_EXCL | O_RDWR));
    if (!writer_.isOpen() || writer_.isDirectory()) return false;
    created_ = true;
    expected_ = length;
    return true;
  }

  bool append(const uint8_t* data, size_t count) {
    if (!created_ || !writer_.isOpen() || !data || !count ||
        count > expected_ - written_) return false;
    if (writer_.write(data, count) != count) return false;
    written_ += count;
    if ((written_ & 0x3fffu) < count) (void)esp_task_wdt_reset();
    return true;
  }

  bool seal() {
    if (!created_ || !writer_.isOpen() || written_ != expected_) return false;
    writer_.flush();
    if (!writer_.close()) return false;
    reader_ = Storage.open(kPackageIntakeStage, O_RDONLY);
    return reader_.isOpen() && !reader_.isDirectory() &&
           reader_.fileSize64() == expected_;
  }

  bool readAt(uint64_t offset, uint8_t* dest, size_t count) {
    if (!reader_.isOpen() || !dest || offset > expected_ ||
        count > expected_ - offset || !reader_.seek64(offset)) return false;
    return reader_.read(dest, count) == static_cast<int>(count);
  }

  bool discard() {
    if (reader_.isOpen()) (void)reader_.close();
    if (writer_.isOpen()) (void)writer_.close();
    if (!created_) return true;
    created_ = false;
    return Storage.remove(kPackageIntakeStage);
  }

 private:
  HalFile writer_{};
  HalFile reader_{};
  uint64_t expected_ = 0;
  uint64_t written_ = 0;
  bool created_ = false;
};

ArchiveStageResult checkFloor(const PackageArchive& archive, bool allowFirstInstall) {
  const FloorCheck status = checkPackageSecurityFloor(devicePackageSecurityFloors(),
                                                     archive, allowFirstInstall);
  if (status == FloorCheck::Allowed) return ArchiveStageResult::ReadyForPublicationReview;
  if (status == FloorCheck::SecurityRollback) return ArchiveStageResult::SecurityRollback;
  return ArchiveStageResult::SecurityFloorUnavailable;
}
} // namespace

ArchiveStageResult stageSignedDevicePackage(std::FILE* source,
    const TrustedPackageSigner* signers, size_t signerCount,
    const PackageRuntimePolicy& policy, PackageCapabilityApi resolveCapability,
    void* resolverContext, PackageVerificationWorkspace& workspace,
    PackageArchive& result, PackageArchiveLimits limits, bool allowFirstInstall,
    uint8_t expectedSignedPrefixDigest[32]) {
  result = {};
  if (expectedSignedPrefixDigest) std::memset(expectedSignedPrefixDigest, 0, 32);
  constexpr uint64_t overhead = kPackageHeaderBytes + kPackageManifestLimit +
      kPackageSignatureBytes;
  if (!source || !signers || !signerCount || signerCount > 16 ||
      !policy.architecture || !policy.runtimeApi || !limits.maxTotalBytes ||
      limits.maxTotalBytes > static_cast<uint64_t>(LONG_MAX) - overhead)
    return ArchiveStageResult::InvalidInput;
  if (std::fseek(source, 0, SEEK_END) != 0) return ArchiveStageResult::InvalidInput;
  const long measured = std::ftell(source);
  if (measured < static_cast<long>(kPackageHeaderBytes) ||
      static_cast<uint64_t>(measured) > limits.maxTotalBytes + overhead)
    return ArchiveStageResult::InvalidInput;
  const uint64_t length = static_cast<uint64_t>(measured);
  const auto sourceRead = [source, length](uint64_t offset, uint8_t* dest,
                                            size_t count) {
    if (offset > length || count > length - offset ||
        offset > static_cast<uint64_t>(LONG_MAX)) return false;
    const bool read = std::fseek(source, static_cast<long>(offset), SEEK_SET) == 0 &&
        std::fread(dest, 1, count, source) == count;
    if (read && (offset & 0x3fffu) < count) (void)esp_task_wdt_reset();
    return read;
  };
  // Resolve the version floor from device-controlled NVS only after complete
  // signature/content authentication; do not stage a known downgrade.
  const PackageInspectionResult inspected = inspectSignedPackage(source, signers,
      signerCount, policy, resolveCapability, resolverContext, workspace, result, limits);
  if (inspected != PackageInspectionResult::ContentVerifiedForInspection) {
    result = {};
    return inspected == PackageInspectionResult::PolicyRejected ?
        ArchiveStageResult::PreflightRejected : ArchiveStageResult::SourceUntrusted;
  }
  const ArchiveStageResult initialFloor = checkFloor(result, allowFirstInstall);
  if (initialFloor != ArchiveStageResult::ReadyForPublicationReview) {
    result = {};
    return initialFloor;
  }
  PackageMbedtlsSha256 hash;
  PackageDeviceTrustVerifier verify(result, signers, signerCount);
  HalArchiveStage stage;
  const ArchiveStageResult staged = stageSignedPackageArchive(sourceRead, length,
      stage, hash, verify,
      [resolveCapability, resolverContext](const char* capability) -> uint32_t {
        return resolveCapability ? resolveCapability(capability, resolverContext) : 0;
      }, policy, result, workspace, limits);
  if (staged != ArchiveStageResult::ReadyForPublicationReview) return staged;
  // A concurrent publisher may have advanced this package's floor during the
  // copy. Refuse and discard the candidate rather than return stale approval.
  const ArchiveStageResult finalFloor = checkFloor(result, allowFirstInstall);
  if (finalFloor != ArchiveStageResult::ReadyForPublicationReview) {
    (void)stage.discard();
    result = {};
    return finalFloor;
  }
  if (expectedSignedPrefixDigest) {
    if (!hash.start() || !hash.update(workspace.signedPrefix,
                                     static_cast<size_t>(result.signatureOffset)) ||
        !hash.finish(expectedSignedPrefixDigest)) {
      (void)stage.discard();
      std::memset(expectedSignedPrefixDigest, 0, 32);
      result = {};
      return ArchiveStageResult::SourceUntrusted;
    }
  }
  return staged;
}

} // namespace RuntimePackages
