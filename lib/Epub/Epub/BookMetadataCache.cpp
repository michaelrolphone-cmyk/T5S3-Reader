#include "BookMetadataCache.h"

#include <Arduino.h>
#if defined(BOARD_XTEINK_X4_PRO) || defined(BOARD_T5S3_PRO)
#include <HalReadBudget.h>
#include <HalWriteBudget.h>
#endif
#include <Logging.h>
#include <Serialization.h>
#include <ZipFile.h>

#include <deque>

#include "FsHelpers.h"

namespace {
// Generation 11 (#389 BUG-173); supersedes gens 8-10.
constexpr uint8_t BOOK_CACHE_VERSION = 11;
constexpr char bookBinFile[] = "/book.bin";
constexpr char tmpSpineBinFile[] = "/spine.bin.tmp";
constexpr char tmpTocBinFile[] = "/toc.bin.tmp";
constexpr int BUILD_YIELD_INTERVAL = 16;

inline void maybeYieldDuringBuild(const int iteration) {
  if (iteration > 0 && (iteration % BUILD_YIELD_INTERVAL) == 0) {
    vTaskDelay(1);
  }
}

// Explicit operation-local opt-in. Scratch creation and warm-cache readers keep
// their original scheduling. These adapters preserve scalar boundaries, field
// widths/order and the existing serialization error behavior; no buffering.
template <typename T>
void writeMetadataPod(FsFile& file, const T& value, HalWriteBudget* budget) {
#if defined(BOARD_XTEINK_X4_PRO) || defined(BOARD_T5S3_PRO)
  if (budget) {
    file.writeCooperatively(reinterpret_cast<const uint8_t*>(&value), sizeof(T), *budget);
    return;
  }
#endif
  (void)budget;
  serialization::writePod(file, value);
}

void writeMetadataString(FsFile& file, const std::string& value, HalWriteBudget* budget) {
#if defined(BOARD_XTEINK_X4_PRO) || defined(BOARD_T5S3_PRO)
  if (budget) {
    const uint32_t length = value.size();
    writeMetadataPod(file, length, budget);
    file.writeCooperatively(reinterpret_cast<const uint8_t*>(value.data()), length, *budget);
    return;
  }
#endif
  (void)budget;
  serialization::writeString(file, value);
}

template <typename T>
void readMetadataPod(FsFile& file, T& value, HalReadBudget* budget) {
#if defined(BOARD_XTEINK_X4_PRO) || defined(BOARD_T5S3_PRO)
  if (budget) {
    serialization::readPod(file, value, *budget);
    return;
  }
#endif
  (void)budget;
  serialization::readPod(file, value);
}

void readMetadataString(FsFile& file, std::string& value, HalReadBudget* budget) {
#if defined(BOARD_XTEINK_X4_PRO) || defined(BOARD_T5S3_PRO)
  if (budget) {
    serialization::readString(file, value, *budget);
    return;
  }
#endif
  (void)budget;
  serialization::readString(file, value);
}
}  // namespace

/* ============= WRITING / BUILDING FUNCTIONS ================ */

bool BookMetadataCache::beginWrite() {
  smallSpineHrefs.clear();
  useSmallSpineHrefs = false;
  buildMode = true;
  spineCount = 0;
  tocCount = 0;
  LOG_DBG("BMC", "Entering write mode");
  return true;
}

bool BookMetadataCache::beginContentOpfPass() {
  LOG_DBG("BMC", "Beginning content opf pass");

  // Open spine file for writing
  return Storage.openFileForWrite("BMC", cachePath + tmpSpineBinFile, spineFile);
}

bool BookMetadataCache::endContentOpfPass() {
  // Explicit close() required: member variable persists beyond function scope
  spineFile.close();
  return true;
}

bool BookMetadataCache::beginTocPass() {
  LOG_DBG("BMC", "Beginning toc pass");
  smallSpineHrefs.clear();
  useSmallSpineHrefs = false;

  if (!Storage.openFileForRead("BMC", cachePath + tmpSpineBinFile, spineFile)) {
    return false;
  }
  if (!Storage.openFileForWrite("BMC", cachePath + tmpTocBinFile, tocFile)) {
    // Explicit close() required: member variable persists beyond function scope
    spineFile.close();
    return false;
  }

  if (spineCount >= LARGE_SPINE_THRESHOLD) {
    spineHrefIndex.clear();
    spineHrefIndex.resize(spineCount);
    spineFile.seek(0);
    for (int i = 0; i < spineCount; i++) {
      auto entry = readSpineEntry(spineFile);
      SpineHrefIndexEntry idx;
      idx.hrefHash = fnvHash64(entry.href);
      idx.hrefLen = static_cast<uint16_t>(entry.href.size());
      idx.spineIndex = static_cast<int16_t>(i);
      spineHrefIndex[i] = idx;
      maybeYieldDuringBuild(i + 1);
    }
    std::sort(spineHrefIndex.begin(), spineHrefIndex.end(),
              [](const SpineHrefIndexEntry& a, const SpineHrefIndexEntry& b) {
                return a.hrefHash < b.hrefHash || (a.hrefHash == b.hrefHash && a.hrefLen < b.hrefLen);
              });
    spineFile.seek(0);
    useSpineHrefIndex = true;
    LOG_DBG("BMC", "Using fast index for %d spine items", spineCount);
  } else {
    useSpineHrefIndex = false;
    // At most 127 strings / 8 KiB of href bytes. Read the immutable scratch
    // spine once, rather than its prefix again for every subsection anchor.
    // This optional acceleration falls back to the existing exact scan on
    // oversized input, incomplete I/O or budget exhaustion; no partial cache.
    constexpr uint32_t INDEX_BUDGET_MS = 1000;
    constexpr uint32_t INDEX_YIELD_MS = 20;
    const uint32_t started = millis();
    uint32_t checkpoint = started;
    size_t hrefBytes = 0;
    for (int i = 0; i < spineCount; ++i) {
      uint32_t length = 0;
      size_t cumulativeSize = 0;
      int16_t tocIndex = -1;
      if (spineFile.read(&length, sizeof(length)) != sizeof(length) ||
          length > SMALL_SPINE_HREF_BYTES - hrefBytes) {
        break;
      }
      std::string href(length, '\0');
      if ((length && spineFile.read(&href[0], length) != static_cast<int>(length)) ||
          spineFile.read(&cumulativeSize, sizeof(cumulativeSize)) != sizeof(cumulativeSize) ||
          spineFile.read(&tocIndex, sizeof(tocIndex)) != sizeof(tocIndex)) {
        break;
      }
      hrefBytes += length;
      smallSpineHrefs.push_back(std::move(href));
      const uint32_t now = millis();
      if ((i + 1) % BUILD_YIELD_INTERVAL == 0 || uint32_t(now - checkpoint) >= INDEX_YIELD_MS) {
        vTaskDelay(1);
        checkpoint = millis();
      }
      if (uint32_t(millis() - started) >= INDEX_BUDGET_MS) {
        break;
      }
    }
    useSmallSpineHrefs = smallSpineHrefs.size() == spineCount;
    if (!useSmallSpineHrefs) {
      smallSpineHrefs.clear();
      smallSpineHrefs.shrink_to_fit();
    }
    spineFile.seek(0);
  }

  return true;
}

bool BookMetadataCache::resetTocEntries() {
  // Keep the already-built spine/index; only discard this failed TOC attempt.
  if (!buildMode || !tocFile.close()) return false;
  if (!Storage.openFileForWrite("BMC", cachePath + tmpTocBinFile, tocFile)) return false;
  tocCount = 0;
  return true;
}

bool BookMetadataCache::endTocPass() {
  // Explicit close() required: member variables persist beyond function scope
  tocFile.close();
  spineFile.close();

  smallSpineHrefs.clear();
  smallSpineHrefs.shrink_to_fit();
  useSmallSpineHrefs = false;
  spineHrefIndex.clear();
  spineHrefIndex.shrink_to_fit();
  useSpineHrefIndex = false;

  return true;
}

bool BookMetadataCache::endWrite() {
  if (!buildMode) {
    LOG_DBG("BMC", "endWrite called but not in build mode");
    return false;
  }

  buildMode = false;
  LOG_DBG("BMC", "Wrote %d spine, %d TOC entries", spineCount, tocCount);
  return true;
}

bool BookMetadataCache::buildBookBin(const std::string& epubPath, const BookMetadata& metadata) {
  // Open all three files, writing to meta, reading from spine and toc
  if (!Storage.openFileForWrite("BMC", cachePath + bookBinFile, bookFile)) {
    return false;
  }

  if (!Storage.openFileForRead("BMC", cachePath + tmpSpineBinFile, spineFile)) {
    // Explicit close() required: member variable persists beyond function scope
    bookFile.close();
    return false;
  }

  if (!Storage.openFileForRead("BMC", cachePath + tmpTocBinFile, tocFile)) {
    // Explicit close() required: member variables persist beyond function scope
    bookFile.close();
    spineFile.close();
    return false;
  }

  // At most 32 successful provider transfers or 4 KiB per budget, with an
  // 8 ms elapsed-time checkpoint and a real scheduler wait. Each HAL call
  // keeps its existing deadline, generation checks and close/retry behavior.
  // Existing per-record CPU-work yields remain in place. The budgets never
  // survive this call or its early exits; no cached bytes/handles are added.
#if defined(BOARD_XTEINK_X4_PRO) || defined(BOARD_T5S3_PRO)
  HalReadBudget readBudget([]() -> uint32_t { return millis(); }, []() { delay(1); });
  HalWriteBudget writeBudget([]() -> uint32_t { return millis(); }, []() { delay(1); });
  auto* reads = &readBudget;
  auto* writes = &writeBudget;
#else
  HalReadBudget* reads = nullptr;
  HalWriteBudget* writes = nullptr;
#endif

  constexpr uint32_t headerASize =
      sizeof(BOOK_CACHE_VERSION) + /* LUT Offset */ sizeof(uint32_t) + sizeof(spineCount) + sizeof(tocCount);
  const uint32_t metadataSize = metadata.title.size() + metadata.author.size() + metadata.language.size() +
                                metadata.coverItemHref.size() + metadata.textReferenceHref.size() +
                                sizeof(uint32_t) * 5;
  const uint32_t lutSize = sizeof(uint32_t) * spineCount + sizeof(uint32_t) * tocCount;
  const uint32_t lutOffset = headerASize + metadataSize;

  // Header A
  writeMetadataPod(bookFile, BOOK_CACHE_VERSION, writes);
  writeMetadataPod(bookFile, lutOffset, writes);
  writeMetadataPod(bookFile, spineCount, writes);
  writeMetadataPod(bookFile, tocCount, writes);
  // Metadata
  writeMetadataString(bookFile, metadata.title, writes);
  writeMetadataString(bookFile, metadata.author, writes);
  writeMetadataString(bookFile, metadata.language, writes);
  writeMetadataString(bookFile, metadata.coverItemHref, writes);
  writeMetadataString(bookFile, metadata.textReferenceHref, writes);

  // Loop through spine entries, writing LUT positions
  spineFile.seek(0);
  for (int i = 0; i < spineCount; i++) {
    uint32_t pos = spineFile.position();
    auto spineEntry = readSpineEntry(spineFile, reads);
    writeMetadataPod(bookFile, pos + lutOffset + lutSize, writes);
    maybeYieldDuringBuild(i + 1);
  }

  // Loop through toc entries, writing LUT positions
  tocFile.seek(0);
  for (int i = 0; i < tocCount; i++) {
    uint32_t pos = tocFile.position();
    auto tocEntry = readTocEntry(tocFile, reads);
    writeMetadataPod(bookFile, pos + lutOffset + lutSize + static_cast<uint32_t>(spineFile.position()), writes);
    maybeYieldDuringBuild(i + 1);
  }

  // LUTs complete
  // Loop through spines from spine file matching up TOC indexes, calculating cumulative size and writing to book.bin

  // Build spineIndex->tocIndex mapping in one pass (O(n) instead of O(n*m))
  std::deque<int16_t> spineToTocIndex(spineCount, -1);
  tocFile.seek(0);
  for (int j = 0; j < tocCount; j++) {
    auto tocEntry = readTocEntry(tocFile, reads);
    if (tocEntry.spineIndex >= 0 && tocEntry.spineIndex < spineCount) {
      if (spineToTocIndex[tocEntry.spineIndex] == -1) {
        spineToTocIndex[tocEntry.spineIndex] = static_cast<int16_t>(j);
      }
    }
    maybeYieldDuringBuild(j + 1);
  }

  ZipFile zip(epubPath);
  // Pre-open zip file to speed up size calculations
  if (!zip.open()) {
    LOG_ERR("BMC", "Could not open EPUB zip for size calculations");
    // Explicit close() required: member variables persist beyond function scope
    bookFile.close();
    spineFile.close();
    tocFile.close();
    return false;
  }
  // NOTE: We intentionally skip calling loadAllFileStatSlims() here.
  // For large EPUBs (2000+ chapters), pre-loading all ZIP central directory entries
  // into memory causes OOM crashes on ESP32-C3's limited ~380KB RAM.
  // Instead, for large books we use a one-pass batch lookup that scans the ZIP
  // central directory once and matches against spine targets using hash comparison.
  // This is O(n*log(m)) instead of O(n*m) while avoiding memory exhaustion.
  // See: https://github.com/crosspoint-reader/crosspoint-reader/issues/134

  std::deque<uint32_t> spineSizes;
  bool useBatchSizes = false;

  if (spineCount >= LARGE_SPINE_THRESHOLD) {
    LOG_DBG("BMC", "Using batch size lookup for %d spine items", spineCount);

    std::deque<ZipFile::SizeTarget> targets;
    targets.resize(spineCount);

    spineFile.seek(0);
    for (int i = 0; i < spineCount; i++) {
      auto entry = readSpineEntry(spineFile, reads);
      std::string path = FsHelpers::normalisePath(entry.href);

      ZipFile::SizeTarget t;
      t.hash = ZipFile::fnvHash64(path.c_str(), path.size());
      t.len = static_cast<uint16_t>(path.size());
      t.index = static_cast<uint16_t>(i);
      targets[i] = t;
      maybeYieldDuringBuild(i + 1);
    }

    std::sort(targets.begin(), targets.end(), [](const ZipFile::SizeTarget& a, const ZipFile::SizeTarget& b) {
      return a.hash < b.hash || (a.hash == b.hash && a.len < b.len);
    });

    spineSizes.resize(spineCount, 0);
    int matched = zip.fillUncompressedSizes(targets, spineSizes);
    LOG_DBG("BMC", "Batch lookup matched %d/%d spine items", matched, spineCount);

    targets.clear();
    targets.shrink_to_fit();

    useBatchSizes = true;
  }

  // Opt in without scanning: prepare only after an ordinary lookup wraps.
  // Sequential-prefix books retain their cheap cursor path. Optional offsets
  // cap at 1024 entries/16 KiB; duplicate members and failures retain scans.
  if (spineCount >= 8 && spineCount < LARGE_SPINE_THRESHOLD) {
    zip.enableSizeLookupOffsets();
  }

  uint32_t cumSize = 0;
  spineFile.seek(0);
  int lastSpineTocIndex = -1;
  for (int i = 0; i < spineCount; i++) {
    auto spineEntry = readSpineEntry(spineFile, reads);

    spineEntry.tocIndex = spineToTocIndex[i];

    // Not a huge deal if we don't fine a TOC entry for the spine entry, this is expected behaviour for EPUBs
    // Logging here is for debugging
    if (spineEntry.tocIndex == -1) {
      LOG_DBG("BMC", "Warning: Could not find TOC entry for spine item %d: %s, using title from last section", i,
              spineEntry.href.c_str());
      spineEntry.tocIndex = lastSpineTocIndex;
    }
    lastSpineTocIndex = spineEntry.tocIndex;

    size_t itemSize = 0;
    if (useBatchSizes) {
      itemSize = spineSizes[i];
      if (itemSize == 0) {
        const std::string path = FsHelpers::normalisePath(spineEntry.href);
        if (!zip.getInflatedFileSize(path.c_str(), &itemSize)) {
          LOG_ERR("BMC", "Warning: Could not get size for spine item: %s", path.c_str());
        }
      }
    } else {
      const std::string path = FsHelpers::normalisePath(spineEntry.href);
      if (!zip.getInflatedFileSize(path.c_str(), &itemSize)) {
        LOG_ERR("BMC", "Warning: Could not get size for spine item: %s", path.c_str());
      }
    }

    cumSize += itemSize;
    spineEntry.cumulativeSize = cumSize;

    // Write out spine data to book.bin
    writeSpineEntry(bookFile, spineEntry, writes);
    maybeYieldDuringBuild(i + 1);
  }
  // Close opened zip file
  zip.close();

  // Loop through toc entries from toc file writing to book.bin
  tocFile.seek(0);
  for (int i = 0; i < tocCount; i++) {
    auto tocEntry = readTocEntry(tocFile, reads);
    writeTocEntry(bookFile, tocEntry, writes);
    maybeYieldDuringBuild(i + 1);
  }

  // Explicit close() required: member variables persist beyond function scope
  bookFile.close();
  spineFile.close();
  tocFile.close();

  LOG_DBG("BMC", "Successfully built book.bin");
  return true;
}

bool BookMetadataCache::cleanupTmpFiles() const {
  const auto spineBinFile = cachePath + tmpSpineBinFile;
  if (Storage.exists(spineBinFile.c_str())) {
    Storage.remove(spineBinFile.c_str());
  }
  const auto tocBinFile = cachePath + tmpTocBinFile;
  if (Storage.exists(tocBinFile.c_str())) {
    Storage.remove(tocBinFile.c_str());
  }
  return true;
}

uint32_t BookMetadataCache::writeSpineEntry(FsFile& file, const SpineEntry& entry, HalWriteBudget* budget) const {
  const uint32_t pos = file.position();
  writeMetadataString(file, entry.href, budget);
  writeMetadataPod(file, entry.cumulativeSize, budget);
  writeMetadataPod(file, entry.tocIndex, budget);
  return pos;
}

uint32_t BookMetadataCache::writeTocEntry(FsFile& file, const TocEntry& entry, HalWriteBudget* budget) const {
  const uint32_t pos = file.position();
  writeMetadataString(file, entry.title, budget);
  writeMetadataString(file, entry.href, budget);
  writeMetadataString(file, entry.anchor, budget);
  writeMetadataPod(file, entry.level, budget);
  writeMetadataPod(file, entry.spineIndex, budget);
  return pos;
}

// Note: for the LUT to be accurate, this **MUST** be called for all spine items before `addTocEntry` is ever called
// this is because in this function we're marking positions of the items
void BookMetadataCache::createSpineEntry(const std::string& href) {
  if (!buildMode || !spineFile) {
    LOG_DBG("BMC", "createSpineEntry called but not in build mode");
    return;
  }

  const SpineEntry entry(href, 0, -1);
  writeSpineEntry(spineFile, entry);
  spineCount++;
}

void BookMetadataCache::createTocEntry(const std::string& title, const std::string& href, const std::string& anchor,
                                       const uint8_t level) {
  if (!buildMode || !tocFile || !spineFile) {
    LOG_DBG("BMC", "createTocEntry called but not in build mode");
    return;
  }

  int16_t spineIndex = -1;

  if (useSmallSpineHrefs) {
    const auto it = std::find(smallSpineHrefs.begin(), smallSpineHrefs.end(), href);
    if (it != smallSpineHrefs.end()) {
      spineIndex = static_cast<int16_t>(it - smallSpineHrefs.begin());
    } else {
      LOG_DBG("BMC", "createTocEntry: Could not find spine item for TOC href %s", href.c_str());
    }
  } else if (useSpineHrefIndex) {
    uint64_t targetHash = fnvHash64(href);
    uint16_t targetLen = static_cast<uint16_t>(href.size());

    auto it =
        std::lower_bound(spineHrefIndex.begin(), spineHrefIndex.end(), SpineHrefIndexEntry{targetHash, targetLen, 0},
                         [](const SpineHrefIndexEntry& a, const SpineHrefIndexEntry& b) {
                           return a.hrefHash < b.hrefHash || (a.hrefHash == b.hrefHash && a.hrefLen < b.hrefLen);
                         });

    while (it != spineHrefIndex.end() && it->hrefHash == targetHash && it->hrefLen == targetLen) {
      spineIndex = it->spineIndex;
      break;
    }

    if (spineIndex == -1) {
      LOG_DBG("BMC", "createTocEntry: Could not find spine item for TOC href %s", href.c_str());
    }
  } else {
    spineFile.seek(0);
    for (int i = 0; i < spineCount; i++) {
      auto spineEntry = readSpineEntry(spineFile);
      if (spineEntry.href == href) {
        spineIndex = static_cast<int16_t>(i);
        break;
      }
    }
    if (spineIndex == -1) {
      LOG_DBG("BMC", "createTocEntry: Could not find spine item for TOC href %s", href.c_str());
    }
  }

  const TocEntry entry(title, href, anchor, level, spineIndex);
  writeTocEntry(tocFile, entry);
  tocCount++;
}

/* ============= READING / LOADING FUNCTIONS ================ */

bool BookMetadataCache::load() {
  if (!Storage.openFileForRead("BMC", cachePath + bookBinFile, bookFile)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(bookFile, version);
  if (version != BOOK_CACHE_VERSION) {
    LOG_DBG("BMC", "Cache version mismatch: expected %d, got %d", BOOK_CACHE_VERSION, version);
    // Explicit close() required: member variable persists beyond function scope
    bookFile.close();
    return false;
  }

  serialization::readPod(bookFile, lutOffset);
  serialization::readPod(bookFile, spineCount);
  serialization::readPod(bookFile, tocCount);

  serialization::readString(bookFile, coreMetadata.title);
  serialization::readString(bookFile, coreMetadata.author);
  serialization::readString(bookFile, coreMetadata.language);
  serialization::readString(bookFile, coreMetadata.coverItemHref);
  serialization::readString(bookFile, coreMetadata.textReferenceHref);

  loaded = true;
  LOG_DBG("BMC", "Loaded cache data: %d spine, %d TOC entries", spineCount, tocCount);
  return true;
}

BookMetadataCache::SpineEntry BookMetadataCache::getSpineEntry(const int index) {
  if (!loaded) {
    LOG_ERR("BMC", "getSpineEntry called but cache not loaded");
    return {};
  }

  if (index < 0 || index >= static_cast<int>(spineCount)) {
    LOG_ERR("BMC", "getSpineEntry index %d out of range", index);
    return {};
  }

  // Seek to spine LUT item, read from LUT and get out data
  bookFile.seek(lutOffset + sizeof(uint32_t) * index);
  uint32_t spineEntryPos;
  serialization::readPod(bookFile, spineEntryPos);
  bookFile.seek(spineEntryPos);
  return readSpineEntry(bookFile);
}

BookMetadataCache::TocEntry BookMetadataCache::getTocEntry(const int index) {
  if (!loaded) {
    LOG_ERR("BMC", "getTocEntry called but cache not loaded");
    return {};
  }

  if (index < 0 || index >= static_cast<int>(tocCount)) {
    LOG_ERR("BMC", "getTocEntry index %d out of range", index);
    return {};
  }

  // Seek to TOC LUT item, read from LUT and get out data
  bookFile.seek(lutOffset + sizeof(uint32_t) * spineCount + sizeof(uint32_t) * index);
  uint32_t tocEntryPos;
  serialization::readPod(bookFile, tocEntryPos);
  bookFile.seek(tocEntryPos);
  return readTocEntry(bookFile);
}

BookMetadataCache::SpineEntry BookMetadataCache::readSpineEntry(FsFile& file, HalReadBudget* budget) const {
  SpineEntry entry;
  readMetadataString(file, entry.href, budget);
  readMetadataPod(file, entry.cumulativeSize, budget);
  readMetadataPod(file, entry.tocIndex, budget);
  return entry;
}

BookMetadataCache::TocEntry BookMetadataCache::readTocEntry(FsFile& file, HalReadBudget* budget) const {
  TocEntry entry;
  readMetadataString(file, entry.title, budget);
  readMetadataString(file, entry.href, budget);
  readMetadataString(file, entry.anchor, budget);
  readMetadataPod(file, entry.level, budget);
  readMetadataPod(file, entry.spineIndex, budget);
  return entry;
}
