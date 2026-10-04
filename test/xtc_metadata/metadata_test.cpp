#include "XtcParser.h"

#include <cstdlib>
#include <iostream>
#include <limits>

using xtc::XtcError;
using xtc::XtcParser;

static void require(bool condition, const char* message) {
  if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

static fixture::Image book(uint64_t offset, bool gray = false, bool metadata = true) {
  fixture::Image image;
  xtc::XtcHeader header{};
  header.magic = gray ? xtc::XTCH_MAGIC : xtc::XTC_MAGIC;
  header.versionMajor = 1;
  header.pageCount = 1;
  header.hasMetadata = metadata;
  header.metadataOffset = offset;
  header.pageTableOffset = 768;
  header.dataOffset = 800;
  image.put(0, &header, sizeof(header));
  const char wrongTitle[] = "Wrong fixed title", wrongAuthor[] = "Wrong fixed author";
  image.put(56, wrongTitle, sizeof(wrongTitle));
  image.put(184, wrongAuthor, sizeof(wrongAuthor));
  const xtc::PageTableEntry entry{800, static_cast<uint32_t>(sizeof(xtc::XtgPageHeader) + (gray ? 16 : 8)), 8, 8};
  image.put(768, &entry, sizeof(entry));
  xtc::XtgPageHeader page{};
  page.magic = gray ? xtc::XTH_MAGIC : xtc::XTG_MAGIC;
  page.width = page.height = 8;
  page.dataSize = gray ? 16 : 8;
  image.put(800, &page, sizeof(page));
  const uint8_t pixels[16] = {0xA5, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
  image.put(800 + sizeof(page), pixels, page.dataSize);
  if (metadata) {
    char title[128] = "Relocated title", author[64] = "Real author";
    image.put(offset, title, sizeof(title));
    image.put(offset + sizeof(title), author, sizeof(author));
  }
  return image;
}

static void setOffset(fixture::Image& image, uint64_t offset) { image.put(16, &offset, sizeof(offset)); }
static void reset() {
  require(fixture::live == 0, "previous operation leaked an open file");
  require(fixture::opens == fixture::closes, "open/close counts differ");
  fixture::clearFaults();
  fixture::images.clear();
}
static void expectFailure(XtcParser& parser, XtcError error) {
  require(parser.open("book") == error, "open did not report the expected error");
  require(!parser.isOpen(), "failed open remained usable");
  require(parser.getLastError() == error, "cleanup lost last error");
  require(parser.getTitle().empty() && parser.getAuthor().empty(), "failed open published stale or partial metadata");
  require(fixture::live == 0, "failed open leaked its file");
}
static void expectMetadata(XtcParser& parser) {
  require(parser.open("book") == XtcError::OK, "valid book failed to open");
  require(parser.getTitle() == "Relocated title", "metadata title ignored advertised offset");
  require(parser.getAuthor() == "Real author", "metadata author ignored advertised offset");
  require(parser.isOpen() && fixture::live == 0, "successful open must release its source handle");
}

int main() {
  // Original regression: the decoy fields at 56/184 must never be returned.
  reset();
  fixture::images["book"] = book(256);
  XtcParser parser;
  expectMetadata(parser);
  parser.close();

  for (bool gray : {false, true}) {
    // Contiguous, legacy-header, relocated and >32-bit metadata locations.
    for (uint64_t offset : {uint64_t(56), uint64_t(48), uint64_t(256), uint64_t(1024), (uint64_t(1) << 32) + 256}) {
      reset();
      fixture::images["book"] = book(offset, gray);
      expectMetadata(parser);
      require(parser.getBitDepth() == (gray ? 2 : 1), "metadata changed format detection");
      require(parser.getWidth() == 8 && parser.getHeight() == 8, "metadata changed page dimensions");
      require(std::find(fixture::seeks.begin(), fixture::seeks.end(), offset) != fixture::seeks.end(), "metadata seek truncated offset");
      uint8_t pixels[16] = {};
      require(parser.loadPage(0, pixels, sizeof(pixels)) == (gray ? 16u : 8u), "normal page load failed");
      require(pixels[0] == 0xA5 && pixels[7] == 7, "metadata changed page bytes");
      size_t streamed = 0;
      require(parser.loadPageStreaming(0, [&](const uint8_t* data, size_t size, size_t position) {
        require(position == streamed, "stream offset changed");
        require(std::memcmp(pixels + position, data, size) == 0, "stream pixels changed");
        streamed += size;
      }, 3) == XtcError::OK, "normal page streaming failed");
      require(streamed == (gray ? 16u : 8u), "stream byte count changed");
      parser.close();
      parser.close();
    }
  }

  // Optional metadata stays optional, including a legacy 48-byte header/table.
  reset();
  fixture::images["book"] = book(UINT64_MAX, false, false);
  require(parser.open("book") == XtcError::OK, "absent metadata inspected irrelevant offset");
  require(parser.getTitle().empty() && parser.getAuthor().empty(), "absent metadata reused earlier values");
  parser.close();
  auto& legacy = fixture::images["book"];
  const uint64_t legacyTable = 48;
  const xtc::PageTableEntry legacyEntry{800, 30, 8, 8};
  legacy.put(24, &legacyTable, sizeof(legacyTable));
  legacy.put(48, &legacyEntry, sizeof(legacyEntry));
  require(parser.open("book") == XtcError::OK, "legacy no-metadata header failed");
  parser.close();

  // Empty, UTF-8 and full-width non-terminated fields remain bounded.
  for (int kind = 0; kind < 3; ++kind) {
    reset(); fixture::images["book"] = book(256);
    char title[128] = {}, author[64] = {};
    if (kind == 1) {
      std::strcpy(title, u8"L'été 日本語"); std::strcpy(author, u8"作者 Émilie");
    } else if (kind == 2) {
      std::memset(title, 'T', sizeof(title)); std::memset(author, 'A', sizeof(author));
    }
    fixture::images["book"].put(256, title, sizeof(title));
    fixture::images["book"].put(384, author, sizeof(author));
    require(parser.open("book") == XtcError::OK, "bounded string fields failed");
    require(parser.getTitle() == std::string(title, strnlen(title, sizeof(title))), "title content changed");
    require(parser.getAuthor() == std::string(author, strnlen(author, sizeof(author))), "author content changed");
    parser.close();
  }

  // Reject missing, header-overlapping, truncated and overflowing metadata regions.
  for (uint64_t offset : {uint64_t(0), uint64_t(47), uint64_t(900), UINT64_MAX, UINT64_MAX - 127}) {
    reset(); fixture::images["book"] = book(256);
    setOffset(fixture::images["book"], offset);
    expectFailure(parser, XtcError::CORRUPTED_HEADER);
    setOffset(fixture::images["book"], 256);
    expectMetadata(parser); parser.close();
  }
  for (uint64_t size : {uint64_t(256), uint64_t(383), uint64_t(384), uint64_t(447)}) {
    reset(); fixture::images["book"] = book(256);
    fixture::images["book"].size = size;
    expectFailure(parser, XtcError::CORRUPTED_HEADER);
    fixture::images["book"] = book(256);
    expectMetadata(parser); parser.close();
  }

  // Seek/read failures, including negative and short positive returns, never publish partial fields.
  for (uint64_t position : {uint64_t(0), uint64_t(256), uint64_t(384), uint64_t(768)}) {
    for (int result : {-1, 0, 1}) {
      reset(); fixture::images["book"] = book(256);
      fixture::failRead = position; fixture::readResult = result;
      expectFailure(parser, XtcError::READ_ERROR);
      fixture::clearFaults();
      expectMetadata(parser); parser.close();
    }
  }
  for (uint64_t position : {uint64_t(256), uint64_t(768)}) {
    reset(); fixture::images["book"] = book(256);
    fixture::failSeek = position;
    expectFailure(parser, XtcError::READ_ERROR);
    fixture::clearFaults(); expectMetadata(parser); parser.close();
  }

  // Failure after success, then repeated failure and a metadata-free retry on the same object.
  reset(); fixture::images["book"] = book(256); expectMetadata(parser);
  fixture::failOpen = true; expectFailure(parser, XtcError::FILE_NOT_FOUND);
  fixture::clearFaults(); fixture::failRead = 384; expectFailure(parser, XtcError::READ_ERROR);
  expectFailure(parser, XtcError::READ_ERROR);
  fixture::clearFaults(); fixture::images["book"] = book(0, false, false);
  require(parser.open("book") == XtcError::OK, "metadata-free retry failed");
  require(parser.getTitle().empty() && parser.getAuthor().empty(), "retry retained earlier metadata");
  parser.close();
  reset();
  std::cout << "XTC metadata offset, normal pages, failure/retry and cleanup regression passed\n";
}
