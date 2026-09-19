#pragma once

#include "PackageOrdinaryManifest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace RuntimePackages {

// Bounded unsigned .rte.zip inspect/pack for ordinary four-kind packages.
// Stored (method 0) entries only. ZIP CRC is transport; SHA-256 lives in
// .package.json. This is not RISC-PKG and does not authenticate a publisher.
constexpr uint32_t kRteZipLocalSig = 0x04034b50u;
constexpr uint32_t kRteZipCentralSig = 0x02014b50u;
constexpr uint32_t kRteZipEocdSig = 0x06054b50u;
constexpr uint16_t kRteZipStored = 0;
constexpr size_t kRteZipLocalBytes = 30;
constexpr size_t kRteZipCentralBytes = 46;
constexpr size_t kRteZipEocdBytes = 22;
constexpr size_t kRteZipMaxName = 127;
constexpr size_t kRteZipMaxEntries = kMaxPackageEntries + 1;
constexpr uint64_t kRteZipMaxEntryBytes = 1024u * 1024u;
constexpr uint64_t kRteZipMaxTotalBytes = 4u * 1024u * 1024u;
constexpr uint16_t kRteZipMaxFiles = 32;

struct RteZipEntry {
  char name[128]{};
  uint32_t crc32 = 0;
  uint32_t sizeBytes = 0;
  uint32_t localHeaderOffset = 0;
  uint32_t dataOffset = 0;
};

struct RteZipView {
  RteZipEntry entries[kRteZipMaxEntries]{};
  uint16_t entryCount = 0;
  uint16_t manifestIndex = 0xffff;
  uint64_t fileLength = 0;
};

enum class RteZipResult : uint8_t {
  Ready, InvalidInput, InvalidHeader, InvalidEntry, UnsafePath,
  DuplicateName, UnsupportedFeature, LimitExceeded, MissingManifest,
  ManifestMismatch, UndeclaredFile, ReadFailure, WriteFailure
};

namespace RteZipDetail {
inline uint16_t le16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(p[1]) << 8;
}
inline uint32_t le32(const uint8_t* p) {
  return static_cast<uint32_t>(le16(p)) | static_cast<uint32_t>(le16(p + 2)) << 16;
}
inline void put16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}
inline void put32(uint8_t* p, uint32_t v) {
  put16(p, static_cast<uint16_t>(v));
  put16(p + 2, static_cast<uint16_t>(v >> 16));
}
inline uint32_t crc32(const uint8_t* data, size_t length) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (unsigned b = 0; b < 8; ++b) {
      const uint32_t mask = static_cast<uint32_t>(-(crc & 1u));
      crc = (crc >> 1) ^ (0xedb88320u & mask);
    }
  }
  return crc ^ 0xffffffffu;
}
inline bool sameFolded(const char* a, const char* b) {
  if (!a || !b) return false;
  for (;;) {
    unsigned char ca = static_cast<unsigned char>(*a++);
    unsigned char cb = static_cast<unsigned char>(*b++);
    if (ca >= 'A' && ca <= 'Z') ca = static_cast<unsigned char>(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = static_cast<unsigned char>(cb - 'A' + 'a');
    if (ca != cb) return false;
    if (!ca) return true;
  }
}
inline bool safeRelativePath(const char* name, size_t length) {
  if (!name || !length || length >= kRteZipMaxName + 1) return false;
  if (name[0] == '/' || name[0] == '\\' || name[length - 1] == '/' ||
      name[length - 1] == '\\')
    return false;
  size_t segment = 0;
  bool dots = true;
  for (size_t i = 0; i < length; ++i) {
    const unsigned char c = static_cast<unsigned char>(name[i]);
    if (c < 0x20 || c > 0x7e || c == '\\' || c == ':') return false;
    if (c == '/') {
      if (!segment || (dots && segment <= 2)) return false;
      segment = 0;
      dots = true;
      continue;
    }
    if (c != '.') dots = false;
    ++segment;
  }
  return segment && !(dots && segment <= 2);
}
} // namespace RteZipDetail

template <typename ReadAt>
RteZipResult inspectRteZip(ReadAt readAt, uint64_t fileLength, RteZipView& out) {
  out = {};
  out.fileLength = fileLength;
  if (fileLength < kRteZipEocdBytes || fileLength > kRteZipMaxTotalBytes + 65536u)
    return RteZipResult::InvalidInput;
  uint8_t eocd[kRteZipEocdBytes]{};
  if (!readAt(fileLength - kRteZipEocdBytes, eocd, sizeof(eocd)))
    return RteZipResult::ReadFailure;
  if (RteZipDetail::le32(eocd) != kRteZipEocdSig) return RteZipResult::InvalidHeader;
  if (RteZipDetail::le16(eocd + 4) || RteZipDetail::le16(eocd + 6) ||
      RteZipDetail::le16(eocd + 20))
    return RteZipResult::UnsupportedFeature;
  const uint16_t entries = RteZipDetail::le16(eocd + 8);
  const uint16_t total = RteZipDetail::le16(eocd + 10);
  const uint32_t cdSize = RteZipDetail::le32(eocd + 12);
  const uint32_t cdOffset = RteZipDetail::le32(eocd + 16);
  if (!entries || entries != total || entries > kRteZipMaxFiles ||
      entries > kRteZipMaxEntries)
    return RteZipResult::LimitExceeded;
  if (static_cast<uint64_t>(cdOffset) + cdSize + kRteZipEocdBytes != fileLength)
    return RteZipResult::InvalidHeader;

  uint64_t cursor = cdOffset;
  uint64_t totalBytes = 0;
  for (uint16_t i = 0; i < entries; ++i) {
    uint8_t central[kRteZipCentralBytes]{};
    if (cursor + kRteZipCentralBytes > fileLength) return RteZipResult::InvalidHeader;
    if (!readAt(cursor, central, sizeof(central))) return RteZipResult::ReadFailure;
    if (RteZipDetail::le32(central) != kRteZipCentralSig)
      return RteZipResult::InvalidHeader;
    const uint16_t flags = RteZipDetail::le16(central + 8);
    const uint16_t method = RteZipDetail::le16(central + 10);
    const uint32_t crc = RteZipDetail::le32(central + 16);
    const uint32_t comp = RteZipDetail::le32(central + 20);
    const uint32_t uncomp = RteZipDetail::le32(central + 24);
    const uint16_t nameLen = RteZipDetail::le16(central + 28);
    const uint16_t extraLen = RteZipDetail::le16(central + 30);
    const uint16_t commentLen = RteZipDetail::le16(central + 32);
    const uint16_t disk = RteZipDetail::le16(central + 34);
    const uint32_t localOff = RteZipDetail::le32(central + 42);
    if (flags || method != kRteZipStored || extraLen || commentLen || disk ||
        comp != uncomp)
      return RteZipResult::UnsupportedFeature;
    if (!nameLen || nameLen > kRteZipMaxName || uncomp > kRteZipMaxEntryBytes)
      return RteZipResult::LimitExceeded;
    if (totalBytes > kRteZipMaxTotalBytes - uncomp) return RteZipResult::LimitExceeded;
    totalBytes += uncomp;

    uint8_t nameBytes[kRteZipMaxName + 1]{};
    if (!readAt(cursor + kRteZipCentralBytes, nameBytes, nameLen))
      return RteZipResult::ReadFailure;
    if (!RteZipDetail::safeRelativePath(reinterpret_cast<const char*>(nameBytes),
                                        nameLen))
      return RteZipResult::UnsafePath;
    nameBytes[nameLen] = 0;
    for (uint16_t prior = 0; prior < i; ++prior) {
      if (RteZipDetail::sameFolded(out.entries[prior].name,
                                   reinterpret_cast<const char*>(nameBytes)))
        return RteZipResult::DuplicateName;
    }

    uint8_t local[kRteZipLocalBytes]{};
    if (static_cast<uint64_t>(localOff) + kRteZipLocalBytes + nameLen + uncomp >
        fileLength)
      return RteZipResult::InvalidEntry;
    if (!readAt(localOff, local, sizeof(local))) return RteZipResult::ReadFailure;
    if (RteZipDetail::le32(local) != kRteZipLocalSig ||
        RteZipDetail::le16(local + 6) ||
        RteZipDetail::le16(local + 8) != kRteZipStored ||
        RteZipDetail::le32(local + 14) != crc ||
        RteZipDetail::le32(local + 18) != uncomp ||
        RteZipDetail::le32(local + 22) != uncomp ||
        RteZipDetail::le16(local + 26) != nameLen ||
        RteZipDetail::le16(local + 28))
      return RteZipResult::InvalidEntry;
    uint8_t localName[kRteZipMaxName + 1]{};
    if (!readAt(static_cast<uint64_t>(localOff) + kRteZipLocalBytes, localName,
                nameLen))
      return RteZipResult::ReadFailure;
    if (std::memcmp(localName, nameBytes, nameLen)) return RteZipResult::InvalidEntry;

    auto& entry = out.entries[i];
    std::memcpy(entry.name, nameBytes, nameLen + 1);
    entry.crc32 = crc;
    entry.sizeBytes = uncomp;
    entry.localHeaderOffset = localOff;
    entry.dataOffset = localOff + kRteZipLocalBytes + nameLen;
    if (!std::strcmp(entry.name, kOrdinaryManifestName)) out.manifestIndex = i;
    cursor += kRteZipCentralBytes + nameLen;
    ordinaryCooperativeYield(cursor, fileLength);
  }
  if (cursor != static_cast<uint64_t>(cdOffset) + cdSize)
    return RteZipResult::InvalidHeader;
  if (out.manifestIndex == 0xffff) return RteZipResult::MissingManifest;
  out.entryCount = entries;
  return RteZipResult::Ready;
}

template <typename ReadAt>
RteZipResult readRteZipEntry(ReadAt readAt, const RteZipView& zip, uint16_t index,
                             uint8_t* dest, size_t capacity) {
  if (index >= zip.entryCount || !dest) return RteZipResult::InvalidInput;
  const auto& entry = zip.entries[index];
  if (entry.sizeBytes > capacity) return RteZipResult::LimitExceeded;
  if (entry.sizeBytes && !readAt(entry.dataOffset, dest, entry.sizeBytes))
    return RteZipResult::ReadFailure;
  if (RteZipDetail::crc32(dest, entry.sizeBytes) != entry.crc32)
    return RteZipResult::InvalidEntry;
  return RteZipResult::Ready;
}

template <typename ReadAt>
RteZipResult planRteZip(ReadAt readAt, const RteZipView& zip,
                        uint8_t* manifestScratch, size_t manifestCapacity,
                        OrdinaryPackagePlan& plan) {
  plan = {};
  if (zip.manifestIndex >= zip.entryCount || !manifestScratch)
    return RteZipResult::MissingManifest;
  const auto& manifest = zip.entries[zip.manifestIndex];
  if (manifest.sizeBytes < 2 || manifest.sizeBytes >= manifestCapacity)
    return RteZipResult::LimitExceeded;
  if (readRteZipEntry(readAt, zip, zip.manifestIndex, manifestScratch,
                      manifestCapacity) != RteZipResult::Ready)
    return RteZipResult::ReadFailure;
  manifestScratch[manifest.sizeBytes] = 0;
  if (!parseOrdinaryManifest(reinterpret_cast<const char*>(manifestScratch),
                             manifest.sizeBytes, plan)) {
    plan = {};
    return RteZipResult::ManifestMismatch;
  }
  if (zip.entryCount != plan.entryCount + 1) {
    plan = {};
    return RteZipResult::UndeclaredFile;
  }
  for (size_t i = 0; i < plan.entryCount; ++i) {
    bool found = false;
    for (uint16_t z = 0; z < zip.entryCount; ++z) {
      if (z == zip.manifestIndex) continue;
      if (std::strcmp(zip.entries[z].name, plan.entries[i].name)) continue;
      if (zip.entries[z].sizeBytes != plan.entries[i].sizeBytes) {
        plan = {};
        return RteZipResult::ManifestMismatch;
      }
      found = true;
      break;
    }
    if (!found) {
      plan = {};
      return RteZipResult::ManifestMismatch;
    }
  }
  return RteZipResult::Ready;
}

struct RteZipSourceFile {
  const char* name = nullptr;
  const uint8_t* bytes = nullptr;
  uint32_t size = 0;
};

template <typename Write>
RteZipResult packRteZip(const RteZipSourceFile* files, size_t count, Write write,
                        uint32_t* written) {
  if (written) *written = 0;
  if (!files || !count || count > kRteZipMaxEntries)
    return RteZipResult::InvalidInput;
  uint32_t localOffsets[kRteZipMaxEntries]{};
  uint32_t crcs[kRteZipMaxEntries]{};
  uint32_t cursor = 0;
  bool sawManifest = false;
  for (size_t i = 0; i < count; ++i) {
    if (!files[i].name || (!files[i].bytes && files[i].size))
      return RteZipResult::InvalidInput;
    const size_t nameLen = std::strlen(files[i].name);
    if (!RteZipDetail::safeRelativePath(files[i].name, nameLen) ||
        files[i].size > kRteZipMaxEntryBytes)
      return RteZipResult::UnsafePath;
    for (size_t prior = 0; prior < i; ++prior)
      if (RteZipDetail::sameFolded(files[prior].name, files[i].name))
        return RteZipResult::DuplicateName;
    if (!std::strcmp(files[i].name, kOrdinaryManifestName)) sawManifest = true;
    crcs[i] = RteZipDetail::crc32(files[i].bytes, files[i].size);
    uint8_t local[kRteZipLocalBytes]{};
    RteZipDetail::put32(local, kRteZipLocalSig);
    RteZipDetail::put16(local + 4, 20);
    RteZipDetail::put16(local + 8, kRteZipStored);
    RteZipDetail::put32(local + 14, crcs[i]);
    RteZipDetail::put32(local + 18, files[i].size);
    RteZipDetail::put32(local + 22, files[i].size);
    RteZipDetail::put16(local + 26, static_cast<uint16_t>(nameLen));
    localOffsets[i] = cursor;
    if (!write(local, sizeof(local)) ||
        !write(reinterpret_cast<const uint8_t*>(files[i].name), nameLen) ||
        (files[i].size && !write(files[i].bytes, files[i].size)))
      return RteZipResult::WriteFailure;
    cursor += static_cast<uint32_t>(sizeof(local) + nameLen + files[i].size);
  }
  if (!sawManifest) return RteZipResult::MissingManifest;
  const uint32_t cdOffset = cursor;
  for (size_t i = 0; i < count; ++i) {
    const size_t nameLen = std::strlen(files[i].name);
    uint8_t central[kRteZipCentralBytes]{};
    RteZipDetail::put32(central, kRteZipCentralSig);
    RteZipDetail::put16(central + 4, 20);
    RteZipDetail::put16(central + 6, 20);
    RteZipDetail::put16(central + 10, kRteZipStored);
    RteZipDetail::put32(central + 16, crcs[i]);
    RteZipDetail::put32(central + 20, files[i].size);
    RteZipDetail::put32(central + 24, files[i].size);
    RteZipDetail::put16(central + 28, static_cast<uint16_t>(nameLen));
    RteZipDetail::put32(central + 42, localOffsets[i]);
    if (!write(central, sizeof(central)) ||
        !write(reinterpret_cast<const uint8_t*>(files[i].name), nameLen))
      return RteZipResult::WriteFailure;
    cursor += static_cast<uint32_t>(sizeof(central) + nameLen);
  }
  uint8_t eocd[kRteZipEocdBytes]{};
  RteZipDetail::put32(eocd, kRteZipEocdSig);
  RteZipDetail::put16(eocd + 8, static_cast<uint16_t>(count));
  RteZipDetail::put16(eocd + 10, static_cast<uint16_t>(count));
  RteZipDetail::put32(eocd + 12, cursor - cdOffset);
  RteZipDetail::put32(eocd + 16, cdOffset);
  if (!write(eocd, sizeof(eocd))) return RteZipResult::WriteFailure;
  if (written) *written = cursor + static_cast<uint32_t>(sizeof(eocd));
  return RteZipResult::Ready;
}

} // namespace RuntimePackages
