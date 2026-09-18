#pragma once

#include "PackagePreflight.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace RuntimePackages {

// An ORDINARY package has only self-declared file-integrity hashes. The
// metadata is never an authorization token or a publisher signature. Offline
// SD sources and online downloads implement precisely the same named-entry
// reader, with no URL, hardware or network logic in the package engine.
constexpr size_t kOrdinaryIoBytes = 512;
constexpr const char* kOrdinaryManifestName = ".package.json";

struct OrdinaryEntry {
  char name[128]{};
  uint64_t sizeBytes = 0;
  char sha256[65]{};
  bool executable = false;
};
struct OrdinaryRequirement {
  char capability[64]{};
  uint32_t minApi = 0;
};
struct OrdinaryPackagePlan {
  Identity identity{};
  char architecture[32]{};
  uint32_t minRuntimeApi = 0;
  OrdinaryEntry entries[kMaxPackageEntries]{};
  size_t entryCount = 0;
  OrdinaryRequirement requirements[kMaxPackageRequirements]{};
  size_t requirementCount = 0;
};

// CPU0's idle task must run even during repeated synchronous SD operations.
// Task watchdog resets alone do not service IDLE0. Keep the package algorithm
// host-testable and yield only on its embedded FreeRTOS implementation.
inline void ordinaryCooperativeYield(uint64_t processedBytes, uint64_t fileBytes) {
#if defined(ESP_PLATFORM)
  // Many native app ELFs are smaller than 16 KiB; always yield after their
  // final chunk so a catalog-wide series of checks cannot starve IDLE0.
  if ((processedBytes & 0x3fffu) == 0u || processedBytes == fileBytes) vTaskDelay(1);
#else
  (void)processedBytes;
  (void)fileBytes;
#endif
}

// ReadAt(name,at,dst,len), EntrySize(name,uint64_t&), Destination begin(plan),
// beginEntry(name,len), append(bytes,len), endEntry(), readEntry(name,at,...),
// writeManifest(bytes,len), seal(), discard(). Hash start/update/finish.
// The destination exclusively creates a fresh, manager-derived temporary
// directory and removes ONLY files created by this invocation on failure.
// Manifest text is validated by a strict bounded caller-side parser before
// passing it here and retained for post-restart verification. No embedded ELF
// or package metadata receives capabilities by virtue of successful staging.
enum class OrdinaryStageResult : uint8_t {
  ReadyForPublicationReview, InvalidInput, PreflightRejected,
  InvalidSourceSize, ReadFailure, IntegrityMismatch, BadElf,
  StageUnavailable, WriteFailure, ReadbackFailure, ManifestFailure, SealFailure
};

inline bool ordinaryDigestEquals(const uint8_t actual[32], const char expected[65]) {
  constexpr char alphabet[] = "0123456789abcdef";
  uint8_t difference = 0;
  for (size_t i = 0; i < 32; ++i) {
    difference |= static_cast<uint8_t>(alphabet[actual[i] >> 4] != expected[2 * i]);
    difference |= static_cast<uint8_t>(alphabet[actual[i] & 15] != expected[2 * i + 1]);
  }
  return difference == 0;
}

// Reuse the same bounds, identity, CPU ABI, version, entry and dependency
// preflight as signed archives, but explicitly omit experimental signer and
// NVS-floor policy. A zero floor does not silently install signing keys.
template <typename Resolver>
PreflightResult preflightOrdinaryPackage(const OrdinaryPackagePlan& plan,
    const PackageRuntimePolicy& limits, Resolver resolver) {
  if (plan.entryCount > kMaxPackageEntries ||
      plan.requirementCount > kMaxPackageRequirements ||
      !plan.architecture[0]) return PreflightResult::InvalidEntryList;
  PackageEntry entries[kMaxPackageEntries]{};
  PackageRequirement needs[kMaxPackageRequirements]{};
  for (size_t i = 0; i < plan.entryCount; ++i) {
    const auto& src = plan.entries[i];
    if (!std::memchr(src.name, 0, sizeof(src.name)) ||
        !std::memchr(src.sha256, 0, sizeof(src.sha256)) ||
        !std::strcmp(src.name, kOrdinaryManifestName) ||
        (src.executable && src.sizeBytes < 52))
      return PreflightResult::InvalidEntry;
    entries[i] = {src.name, src.sizeBytes, src.sha256, src.executable};
  }
  for (size_t i = 0; i < plan.requirementCount; ++i) {
    const auto& src = plan.requirements[i];
    if (!std::memchr(src.capability, 0, sizeof(src.capability)))
      return PreflightResult::InvalidRequirement;
    needs[i] = {src.capability, src.minApi};
  }
  if (!std::memchr(plan.architecture, 0, sizeof(plan.architecture)))
    return PreflightResult::UnsupportedArchitecture;
  const PackageEnvelopeView envelope{plan.identity, plan.architecture,
      plan.minRuntimeApi, 0, entries, plan.entryCount, needs, plan.requirementCount};
  PackageRuntimePolicy ordinary = limits;
  ordinary.minimumSecurityVersion = 0;
  return preflightPackage(envelope, ordinary, resolver);
}

inline bool ordinaryElfHeader(const uint8_t* data, size_t size,
                              const char* architecture) {
  if (!data || size < 52 || !architecture ||
      std::memcmp(data, "\x7f" "ELF\x01\x01", 6) ||
      data[6] != 1 || data[16] != 3 || data[17] != 0 ||
      data[20] != 1 || data[21] || data[22] || data[23]) return false;
  const uint16_t machine = static_cast<uint16_t>(data[18]) |
                           static_cast<uint16_t>(data[19] << 8);
  return (std::strcmp(architecture, "xtensa-esp32s3") == 0 && machine == 94) ||
         (std::strcmp(architecture, "riscv32") == 0 && machine == 243);
}

template <typename Source, typename Destination, typename Hash,
          typename Resolver>
OrdinaryStageResult stageOrdinaryPackage(const OrdinaryPackagePlan& plan,
    const uint8_t* manifest, size_t manifestBytes, Source& source,
    Destination& destination, Hash& hash, Resolver resolver,
    const PackageRuntimePolicy& limits, uint8_t (&io)[kOrdinaryIoBytes]) {
  if (!manifest || !manifestBytes || manifestBytes > 4096 ||
      !limits.maxTotalBytes || !limits.maxEntryBytes)
    return OrdinaryStageResult::InvalidInput;
  if (preflightOrdinaryPackage(plan, limits, resolver) !=
      PreflightResult::ReadyForContentVerification)
    return OrdinaryStageResult::PreflightRejected;
  // Refuse source length conflicts before opening/creating ANY destination.
  // A source reader must also fail on short reads or vanishing/offline media.
  for (size_t i = 0; i < plan.entryCount; ++i) {
    uint64_t actual = 0;
    if (!source.entrySize(plan.entries[i].name, actual) ||
        actual != plan.entries[i].sizeBytes)
      return OrdinaryStageResult::InvalidSourceSize;
  }
  if (!destination.begin(plan)) {
    // begin() has NOT granted ownership. In particular a pre-existing stage
    // must survive a competing or unsuccessful begin intact. Only a destination
    // that acquired exclusive ownership may discard its own files.
    return OrdinaryStageResult::StageUnavailable;
  }
  auto fail = [&destination](OrdinaryStageResult error) {
    (void)destination.discard();
    return error;
  };
  for (size_t i = 0; i < plan.entryCount; ++i) {
    const OrdinaryEntry& entry = plan.entries[i];
    if (!destination.beginEntry(entry.name, entry.sizeBytes) || !hash.start())
      return fail(OrdinaryStageResult::WriteFailure);
    uint64_t at = 0;
    while (at < entry.sizeBytes) {
      const size_t count = entry.sizeBytes - at < sizeof(io) ?
          static_cast<size_t>(entry.sizeBytes - at) : sizeof(io);
      if (!source.readAt(entry.name, at, io, count))
        return fail(OrdinaryStageResult::ReadFailure);
      if (entry.executable && at == 0 &&
          !ordinaryElfHeader(io, count, plan.architecture))
        return fail(OrdinaryStageResult::BadElf);
      if (!hash.update(io, count) || !destination.append(io, count))
        return fail(OrdinaryStageResult::WriteFailure);
      at += count;
      ordinaryCooperativeYield(at, entry.sizeBytes);
    }
    uint8_t digest[32]{};
    if (!hash.finish(digest) || !ordinaryDigestEquals(digest, entry.sha256))
      return fail(OrdinaryStageResult::IntegrityMismatch);
    if (!destination.endEntry() || !hash.start())
      return fail(OrdinaryStageResult::ReadbackFailure);
    at = 0;
    while (at < entry.sizeBytes) {
      const size_t count = entry.sizeBytes - at < sizeof(io) ?
          static_cast<size_t>(entry.sizeBytes - at) : sizeof(io);
      if (!destination.readEntry(entry.name, at, io, count) ||
          !hash.update(io, count))
        return fail(OrdinaryStageResult::ReadbackFailure);
      at += count;
      ordinaryCooperativeYield(at, entry.sizeBytes);
    }
    if (!hash.finish(digest) || !ordinaryDigestEquals(digest, entry.sha256))
      return fail(OrdinaryStageResult::ReadbackFailure);
  }
  if (!destination.writeManifest(manifest, manifestBytes))
    return fail(OrdinaryStageResult::ManifestFailure);
  if (!destination.seal()) return fail(OrdinaryStageResult::SealFailure);
  return OrdinaryStageResult::ReadyForPublicationReview;
}

// A post-restart directory verifier must independently reparse its retained
// ordinary manifest, enumerate EXACT entries, hash every file and reapply
// current runtime policy. The file-integrity digest is not an identity grant.
template <typename Directory, typename Hash, typename Resolver>
bool verifyOrdinaryDirectory(const OrdinaryPackagePlan& plan,
    Directory& directory, Hash& hash, Resolver resolver,
    const PackageRuntimePolicy& limits, uint8_t (&io)[kOrdinaryIoBytes]) {
  if (preflightOrdinaryPackage(plan, limits, resolver) !=
      PreflightResult::ReadyForContentVerification ||
      !directory.exactEntries(plan)) return false;
  for (size_t i = 0; i < plan.entryCount; ++i) {
    const OrdinaryEntry& entry = plan.entries[i];
    uint64_t size = 0;
    if (!directory.entrySize(entry.name, size) || size != entry.sizeBytes ||
        !hash.start()) return false;
    uint64_t at = 0;
    while (at < entry.sizeBytes) {
      const size_t count = entry.sizeBytes - at < sizeof(io) ?
          static_cast<size_t>(entry.sizeBytes - at) : sizeof(io);
      if (!directory.readAt(entry.name, at, io, count) ||
          (entry.executable && !at && !ordinaryElfHeader(io, count, plan.architecture)) ||
          !hash.update(io, count)) return false;
      at += count;
      ordinaryCooperativeYield(at, entry.sizeBytes);
    }
    uint8_t digest[32]{};
    if (!hash.finish(digest) || !ordinaryDigestEquals(digest, entry.sha256))
      return false;
  }
  return directory.exactEntries(plan);
}

} // namespace RuntimePackages
