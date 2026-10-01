#pragma once

#include "PackageOrdinaryInstaller.h"
#include "PackageRteZip.h"
#include <algorithm>
#include <memory>
#include <new>

namespace RuntimePackages {

// Named-entry reader over an inspected stored .rte.zip. Same interface the
// ordinary stager already uses for SD directories and online downloads.
template <typename ReadAt>
struct RteZipSource {
  ReadAt* readAt = nullptr;
  const RteZipView* zip = nullptr;
  bool entrySize(const char* name, uint64_t& size) const {
    if (!readAt || !zip || !name) return false;
    for (uint16_t i = 0; i < zip->entryCount; ++i) {
      if (std::strcmp(zip->entries[i].name, name)) continue;
      size = zip->entries[i].sizeBytes;
      return true;
    }
    return false;
  }
  bool readAtName(const char* name, uint64_t at, uint8_t* dest, size_t count) const {
    if (!readAt || !zip || !name || !dest) return false;
    for (uint16_t i = 0; i < zip->entryCount; ++i) {
      const auto& entry = zip->entries[i];
      if (std::strcmp(entry.name, name)) continue;
      if (at > entry.sizeBytes || count > entry.sizeBytes - static_cast<size_t>(at))
        return false;
      return (*readAt)(entry.dataOffset + at, dest, count);
    }
    return false;
  }
};

// Inspect + bind + stage/publish through the existing ordinary engine.
// Archive bytes stay outside the stream registry; SHA-256 is the ordinary
// manifest digest, not a publisher signature. Keep multi-kilobyte views on
// the heap, not the ESP32 loopTask stack (the old nested hashing path crashed).
template <typename ReadAt, typename Destination, typename Hash,
          typename Resolver, typename Ops, typename Verify, typename Purge>
OrdinaryInstallOutcome installOrdinaryFromRteZip(
    ReadAt readAt, uint64_t fileLength, uint8_t* manifestScratch,
    size_t manifestCapacity, Destination& destination, Hash& hash,
    Resolver resolver, const PackageRuntimePolicy& policy,
    uint8_t (&io)[kOrdinaryIoBytes], Ops& ops, Verify verifyDirectory,
    Purge purgeManagedBackup, bool replacementAllowed) {
  OrdinaryInstallOutcome outcome{};
  std::unique_ptr<RteZipView> zip(new (std::nothrow) RteZipView{});
  std::unique_ptr<OrdinaryPackagePlan> plan(new (std::nothrow) OrdinaryPackagePlan{});
  if (!zip || !plan || !manifestScratch || !manifestCapacity ||
      inspectRteZip(readAt, fileLength, *zip) != RteZipResult::Ready ||
      !RteZipInstallDetail::validateArchive(readAt, *zip) ||
      planRteZip(readAt, *zip, manifestScratch, manifestCapacity, *plan) !=
          RteZipResult::Ready) return outcome;
  RteZipSource<ReadAt> source{&readAt, zip.get()};
  struct NamedSource {
    RteZipSource<ReadAt>* inner;
    bool entrySize(const char* name, uint64_t& size) {
      return inner->entrySize(name, size);
    }
    bool readAt(const char* name, uint64_t at, uint8_t* dest, size_t count) {
      return inner->readAtName(name, at, dest, count);
    }
  } named{&source};
  return installOrdinaryPackage(*plan, manifestScratch,
      zip->entries[zip->manifestIndex].sizeBytes, named, destination, hash,
      resolver, policy, io, ops, verifyDirectory, purgeManagedBackup,
      replacementAllowed);
}

} // namespace RuntimePackages
