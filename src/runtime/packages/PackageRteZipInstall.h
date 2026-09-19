#pragma once

#include "PackageOrdinaryInstaller.h"
#include "PackageRteZip.h"
#include <algorithm>
#include <memory>
#include <new>

namespace RuntimePackages {

// Inspecting central/local headers alone does not establish that the stored
// data is intact. The supported deterministic ZIP subset has contiguous local
// entries in central-directory order followed immediately by the directory.
// This rejects overlapping payloads, hidden gaps and data disguised as headers.
namespace RteZipInstallDetail {
inline uint32_t updateCrc(uint32_t crc, const uint8_t* bytes, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    crc ^= bytes[i];
    for (unsigned bit = 0; bit < 8; ++bit) {
      const uint32_t mask = 0u - (crc & 1u);
      crc = (crc >> 1) ^ (0xedb88320u & mask);
    }
  }
  return crc;
}

template <typename ReadAt>
bool validateArchive(ReadAt& readAt, const RteZipView& zip) {
  if (!zip.entryCount || zip.manifestIndex >= zip.entryCount ||
      zip.fileLength < kRteZipEocdBytes) return false;
  uint8_t eocd[kRteZipEocdBytes]{};
  if (!readAt(zip.fileLength - kRteZipEocdBytes, eocd, sizeof(eocd)) ||
      RteZipDetail::le32(eocd) != kRteZipEocdSig) return false;
  const uint64_t centralOffset = RteZipDetail::le32(eocd + 16);
  uint64_t expectedLocalOffset = 0;
  uint8_t chunk[kOrdinaryIoBytes]{};
#if defined(ESP_PLATFORM)
  TickType_t lastYield = xTaskGetTickCount();
#endif
  for (uint16_t i = 0; i < zip.entryCount; ++i) {
    const auto& entry = zip.entries[i];
    const uint64_t end = static_cast<uint64_t>(entry.dataOffset) + entry.sizeBytes;
    if (entry.localHeaderOffset != expectedLocalOffset ||
        entry.dataOffset != expectedLocalOffset + kRteZipLocalBytes +
                                std::strlen(entry.name) ||
        end > centralOffset || end > zip.fileLength) return false;
    uint32_t crc = 0xffffffffu;
    for (uint64_t offset = 0; offset < entry.sizeBytes;) {
      const size_t count = static_cast<size_t>(std::min<uint64_t>(
          sizeof(chunk), entry.sizeBytes - offset));
      if (!readAt(entry.dataOffset + offset, chunk, count)) return false;
      crc = updateCrc(crc, chunk, count);
      offset += count;
      // Byte and elapsed-time checkpoints: SHA/ELF checks still occur later
      // inside the ordinary stager; neither CRC nor SHA authorizes an ELF.
      ordinaryCooperativeYield(offset, entry.sizeBytes);
#if defined(ESP_PLATFORM)
      const TickType_t now = xTaskGetTickCount();
      if (now - lastYield >= pdMS_TO_TICKS(10)) {
        vTaskDelay(1);
        lastYield = xTaskGetTickCount();
      }
#endif
    }
    if ((crc ^ 0xffffffffu) != entry.crc32) return false;
    expectedLocalOffset = end;
  }
  return expectedLocalOffset == centralOffset;
}
} // namespace RteZipInstallDetail

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
