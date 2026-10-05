// Compile the complete production validator/flashing entry point with in-memory
// storage/partition fixtures. SHA-256 is real OpenSSL via the mbedTLS test adapter.
#include "Fixture.h"
#include "FirmwareFlasher.h"
#include "esp_app_format.h"

#include <fstream>
#include <iostream>
#include <iterator>

namespace ota_boot {
bool switchTo(const esp_partition_t*) { ++switchCalls; return !failSwitch; }
}

namespace {
using firmware_flash::Result;
std::string fixtureDirectory;
size_t cases = 0;

void reset(const std::vector<uint8_t>& bytes) {
  assert(openCount == 0 && hashCount == 0);
  imageBytes = bytes;
  writtenBytes.clear();
  readBytes = readCalls = closeCalls = hashInitCalls = eraseCalls = writeCalls = switchCalls = 0;
  failReadAt = -1;
  failOpen = failSeek = noPartition = failErase = failWrite = failSwitch = false;
}

std::vector<uint8_t> fixture(unsigned segments, bool sha) {
  std::ifstream file(fixtureDirectory + "/" + std::to_string(segments) + (sha ? "-sha.bin" : "-xor.bin"),
                     std::ios::binary);
  assert(file);
  return {std::istreambuf_iterator<char>(file), {}};
}

void expect(Result actual, Result expected, const char* label) {
  ++cases;
  if (actual != expected) {
    std::cerr << label << ": expected " << firmware_flash::resultName(expected)
              << ", got " << firmware_flash::resultName(actual) << '\n';
    std::exit(1);
  }
  assert(openCount == 0 && hashCount == 0);
}

void noFlash() { assert(eraseCalls == 0 && writeCalls == 0 && switchCalls == 0 && writtenBytes.empty()); }

void retryValid(const std::vector<uint8_t>& good) {
  reset(good);
  expect(firmware_flash::validateImageFile("/candidate.bin", partition.size), Result::OK, "valid retry");
  assert(closeCalls == 1);
  noFlash();
}

void progress(size_t written, size_t total, void* context) {
  auto* last = static_cast<size_t*>(context);
  assert(written > *last && written <= total && total == imageBytes.size());
  *last = written;
}
}  // namespace

int main(int argc, char** argv) {
  assert(argc == 2);
  static_assert(ESP_IMAGE_MAX_SEGMENTS == 16, "Update boundary fixtures when the SDK changes");
  fixtureDirectory = argv[1];

  // The regression comes first so compiling against original source proves
  // acceptance of a complete, checksummed and SHA-256-checked 17-segment image.
  reset(fixture(17, true));
  expect(firmware_flash::validateImageFile("/candidate.bin", partition.size), Result::BAD_SEGMENTS,
         "17-segment image");
  assert(readCalls == 1 && readBytes == 24 && closeCalls == 1 && hashInitCalls == 0);
  noFlash();

  for (bool sha : {false, true}) {
    for (unsigned count : {1U, 16U, 17U, 255U}) {
      const auto bytes = fixture(count, sha);
      const Result wanted = count > ESP_IMAGE_MAX_SEGMENTS ? Result::BAD_SEGMENTS : Result::OK;
      reset(bytes);
      expect(firmware_flash::validateImageFile("/candidate.bin", partition.size), wanted, "count boundary");
      assert(closeCalls == 1);
      noFlash();
      if (wanted != Result::OK) assert(readCalls == 1 && readBytes == 24 && hashInitCalls == 0);

      reset(bytes);
      size_t progressed = 0;
      expect(firmware_flash::flashFromSdPath("/candidate.bin", progress, &progressed), wanted,
             "guarded flash entry point");
      if (wanted != Result::OK) {
        noFlash();
        assert(progressed == 0 && closeCalls == 1 && readCalls == 1 && hashInitCalls == 0);
      } else {
        assert(writtenBytes == bytes && eraseCalls > 0 && writeCalls > 0 && switchCalls == 1);
        assert(progressed == bytes.size() && closeCalls == 2);
      }
      retryValid(fixture(16, sha));
    }
  }

  const auto good = fixture(16, true);
  reset(good);
  failOpen = true;
  expect(firmware_flash::validateImageFile("/candidate.bin", partition.size), Result::OPEN_FAIL, "open failure");
  assert(closeCalls == 0); noFlash(); retryValid(good);

  // Header, segment-header and payload read failures are distinct cleanup paths.
  for (int failedRead : {0, 1, 2}) {
    reset(good); failReadAt = failedRead;
    expect(firmware_flash::validateImageFile("/candidate.bin", partition.size), Result::READ_FAIL, "read failure");
    assert(closeCalls == 1); noFlash(); retryValid(good);
  }
  reset(good); imageBytes[0] = 0;
  expect(firmware_flash::validateImageFile("/candidate.bin", partition.size), Result::BAD_MAGIC, "bad magic");
  assert(closeCalls == 1 && hashInitCalls == 0); noFlash(); retryValid(good);

  reset(good); imageBytes.resize(100);
  expect(firmware_flash::validateImageFile("/candidate.bin", partition.size), Result::TOO_SMALL, "small image");
  assert(closeCalls == 1); noFlash(); retryValid(good);

  reset(good);
  expect(firmware_flash::validateImageFile("/candidate.bin", good.size() - 1), Result::TOO_LARGE, "partition bound");
  assert(closeCalls == 1); noFlash(); retryValid(good);

  reset(good); std::fill(imageBytes.begin() + 28, imageBytes.begin() + 32, 0xff);
  expect(firmware_flash::validateImageFile("/candidate.bin", partition.size), Result::BAD_SEGMENTS, "bad segment length");
  assert(closeCalls == 1); noFlash(); retryValid(good);

  reset(good); imageBytes.push_back(0);
  expect(firmware_flash::validateImageFile("/candidate.bin", partition.size), Result::BAD_SIZE, "trailing byte");
  assert(closeCalls == 1); noFlash(); retryValid(good);

  reset(good); imageBytes[40] ^= 1;
  expect(firmware_flash::validateImageFile("/candidate.bin", partition.size), Result::BAD_CHECKSUM, "bad checksum");
  assert(closeCalls == 1); noFlash(); retryValid(good);

  reset(good); imageBytes.back() ^= 1;
  expect(firmware_flash::validateImageFile("/candidate.bin", partition.size), Result::BAD_SHA, "bad SHA");
  assert(closeCalls == 1); noFlash(); retryValid(good);

  reset(good); failSeek = true;
  expect(firmware_flash::validateImageFile("/candidate.bin", partition.size), Result::BOARD_MISMATCH, "marker lookup failure");
  assert(closeCalls == 1); noFlash(); retryValid(good);

  reset(good); noPartition = true;
  expect(firmware_flash::flashFromSdPath("/candidate.bin", nullptr, nullptr), Result::NO_PARTITION, "no partition");
  assert(closeCalls == 0); noFlash(); retryValid(good);

  for (int stage = 0; stage < 3; ++stage) {
    reset(good);
    failErase = stage == 0; failWrite = stage == 1; failSwitch = stage == 2;
    const Result wanted = stage == 0 ? Result::ERASE_FAIL : stage == 1 ? Result::WRITE_FAIL : Result::OTADATA_FAIL;
    expect(firmware_flash::flashFromSdPath("/candidate.bin", nullptr, nullptr), wanted, "flash failure");
    assert(closeCalls == 2 && switchCalls == (stage == 2 ? 1U : 0U));
    if (stage == 0) assert(writeCalls == 0);
    reset(good);
    expect(firmware_flash::flashFromSdPath("/candidate.bin", nullptr, nullptr), Result::OK, "flash retry");
    assert(writtenBytes == good && switchCalls == 1 && closeCalls == 2);
  }

  // The unchanged already-validated path remains usable after a successful check.
  retryValid(good);
  expect(firmware_flash::flashFromSdPath("/candidate.bin", nullptr, nullptr, true), Result::OK, "prevalidated flow");
  assert(writtenBytes == good && closeCalls == 2 && switchCalls == 1);
  std::cout << "Firmware segment-limit production regression: " << cases << " cases passed\n";
}
