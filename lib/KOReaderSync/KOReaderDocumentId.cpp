#include <KOReaderDocumentId.h>

#include <HalStorage.h>
#include <Logging.h>
#include <MD5Builder.h>

#include <algorithm>
#include <cctype>

namespace {

constexpr size_t CHUNK_SIZE = 1024;
constexpr size_t SAMPLE_COUNT = 12;

// Sample offsets matching KOReader's document ID algorithm
constexpr size_t SAMPLE_OFFSETS[SAMPLE_COUNT] = {
    0,          // Start of file
    1024,       // 1 KiB
    4096,       // 4 KiB
    16384,      // 16 KiB
    65536,      // 64 KiB
    262144,     // 256 KiB
    1048576,    // 1 MiB
    4194304,    // 4 MiB
    16777216,   // 16 MiB
    67108864,   // 64 MiB
    268435456,  // 256 MiB
    1073741824  // 1 GiB
};

}  // namespace

std::string KOReaderDocumentId::calculate(const std::string& filePath) {
  FsFile file;
  if (!Storage.openFileForRead("SD", filePath, file)) {
    LOG_DBG("KODoc", "Failed to open file for hashing: %s", filePath.c_str());
    return "";
  }

  const size_t fileSize = file.fileSize();
  if (fileSize == 0) {
    // Empty file - hash empty content
    MD5Builder md5;
    md5.begin();
    md5.calculate();
    return md5.toString();
  }

  MD5Builder md5;
  md5.begin();

  uint8_t buffer[CHUNK_SIZE];
  size_t totalBytesRead = 0;

  for (size_t i = 0; i < SAMPLE_COUNT; ++i) {
    const size_t offset = SAMPLE_OFFSETS[i];

    // Skip samples beyond file size
    if (offset >= fileSize) {
      continue;
    }

    // Seek to offset
    if (!file.seekSet(offset)) {
      LOG_DBG("KODoc", "Failed to seek to offset %zu", offset);
      return "";
    }

    // Read up to CHUNK_SIZE bytes
    const size_t bytesToRead = std::min(CHUNK_SIZE, fileSize - offset);
    const int bytesRead = file.read(buffer, bytesToRead);
    if (bytesRead != static_cast<int>(bytesToRead)) {
      LOG_DBG("KODoc", "Incomplete sample at offset %zu: read %d of %zu bytes", offset, bytesRead, bytesToRead);
      return "";
    }
    md5.add(buffer, bytesRead);
    totalBytesRead += bytesRead;
  }

  // Calculate final hash
  md5.calculate();
  const std::string hash = md5.toString();

  LOG_DBG("KODoc", "Document ID: %s (%zu bytes sampled from %zu file)", hash.c_str(), totalBytesRead,
          fileSize);

  return hash;
}

std::string KOReaderDocumentId::calculateFromFilename(const std::string& filePath) {
  // Extract just the filename from the path
  size_t lastSlash = filePath.find_last_of("/");
  std::string filename = (lastSlash != std::string::npos) ? filePath.substr(lastSlash + 1) : filePath;

  if (filename.empty()) {
    return "";
  }

  MD5Builder md5;
  md5.begin();
  md5.add(filename.c_str());
  md5.calculate();
  return md5.toString();
}
