#include "Txt.h"

#include <FsHelpers.h>
#include <JpegToBmpConverter.h>
#include <Logging.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

Txt::Txt(std::string path, std::string cacheBasePath)
    : filepath(std::move(path)), cacheBasePath(std::move(cacheBasePath)) {
  // Generate cache path from file path hash
  const size_t hash = std::hash<std::string>{}(filepath);
  cachePath = this->cacheBasePath + "/txt_" + std::to_string(hash);
}

bool Txt::load() {
  if (loaded) {
    return true;
  }

  if (!Storage.exists(filepath.c_str())) {
    LOG_ERR("TXT", "File does not exist: %s", filepath.c_str());
    return false;
  }

  FsFile file;
  if (!Storage.openFileForRead("TXT", filepath, file)) {
    LOG_ERR("TXT", "Failed to open file: %s", filepath.c_str());
    return false;
  }

  fileSize = file.size();
  file.close();

  loaded = true;
  LOG_DBG("TXT", "Loaded TXT file: %s (%zu bytes)", filepath.c_str(), fileSize);
  return true;
}

std::string Txt::getTitle() const {
  // Extract filename without path and extension
  size_t lastSlash = filepath.find_last_of('/');
  std::string filename = (lastSlash != std::string::npos) ? filepath.substr(lastSlash + 1) : filepath;

  // Strip a known plain-text extension so "notes.md" and "notes.txt" share a title.
  if (FsHelpers::hasTxtExtension(filename)) {
    filename = filename.substr(0, filename.length() - 4);
  } else if (FsHelpers::hasMarkdownExtension(filename)) {
    filename = filename.substr(0, filename.length() - 3);
  }

  return filename;
}

void Txt::setupCacheDir() const {
  if (!Storage.exists(cacheBasePath.c_str())) {
    Storage.mkdir(cacheBasePath.c_str());
  }
  if (!Storage.exists(cachePath.c_str())) {
    Storage.mkdir(cachePath.c_str());
  }
}

std::string Txt::findCoverImage() const {
  // Get the folder containing the txt file
  size_t lastSlash = filepath.find_last_of('/');
  std::string folder = (lastSlash != std::string::npos) ? filepath.substr(0, lastSlash) : "";
  if (folder.empty()) {
    folder = "/";
  }

  // Get the base filename without extension (e.g., "mybook" from "/books/mybook.txt")
  std::string baseName = getTitle();

  // Image extensions to try
  const char* extensions[] = {".bmp", ".jpg", ".jpeg", ".png", ".BMP", ".JPG", ".JPEG", ".PNG"};

  // First priority: look for image with same name as txt file (e.g., mybook.jpg)
  for (const auto& ext : extensions) {
    std::string coverPath = folder + "/" + baseName + ext;
    if (Storage.exists(coverPath.c_str())) {
      LOG_DBG("TXT", "Found matching cover image: %s", coverPath.c_str());
      return coverPath;
    }
  }

  // Fallback: look for cover image files
  const char* coverNames[] = {"cover", "Cover", "COVER"};
  for (const auto& name : coverNames) {
    for (const auto& ext : extensions) {
      std::string coverPath = folder + "/" + std::string(name) + ext;
      if (Storage.exists(coverPath.c_str())) {
        LOG_DBG("TXT", "Found fallback cover image: %s", coverPath.c_str());
        return coverPath;
      }
    }
  }

  return "";
}

std::string Txt::getCoverBmpPath() const { return cachePath + "/cover.bmp"; }

bool Txt::generateCoverBmp() const {
  // Already generated, return true
  if (Storage.exists(getCoverBmpPath().c_str())) {
    return true;
  }

  std::string coverImagePath = findCoverImage();
  if (coverImagePath.empty()) {
    LOG_DBG("TXT", "No cover image found for TXT file");
    return false;
  }

  // Setup cache directory
  setupCacheDir();

  if (FsHelpers::hasBmpExtension(coverImagePath)) {
    // Copy BMP file to cache
    LOG_DBG("TXT", "Copying BMP cover image to cache");
    FsFile src, dst;
    if (!Storage.openFileForRead("TXT", coverImagePath, src)) {
      return false;
    }
    if (!Storage.openFileForWrite("TXT", getCoverBmpPath(), dst)) {
      return false;
    }
    uint8_t buffer[1024];
    while (src.available()) {
      size_t bytesRead = src.read(buffer, sizeof(buffer));
      dst.write(buffer, bytesRead);
    }
    LOG_DBG("TXT", "Copied BMP cover to cache");
    return true;
  } else if (FsHelpers::hasJpgExtension(coverImagePath)) {
    // Convert JPG/JPEG to BMP (same approach as Epub)
    LOG_DBG("TXT", "Generating BMP from JPG cover image");
    FsFile coverJpg, coverBmp;
    if (!Storage.openFileForRead("TXT", coverImagePath, coverJpg)) {
      return false;
    }
    if (!Storage.openFileForWrite("TXT", getCoverBmpPath(), coverBmp)) {
      return false;
    }
    const bool success = JpegToBmpConverter::jpegFileToBmpStream(coverJpg, coverBmp);

    if (!success) {
      LOG_ERR("TXT", "Failed to generate BMP from JPG cover image");
      Storage.remove(getCoverBmpPath().c_str());
    } else {
      LOG_DBG("TXT", "Generated BMP from JPG cover image");
    }
    return success;
  }

  // PNG files are not supported (would need a PNG decoder)
  LOG_ERR("TXT", "Cover image format not supported (only BMP/JPG/JPEG)");
  return false;
}

bool Txt::readContent(uint8_t* buffer, size_t offset, size_t length) const {
  if (!loaded) {
    return false;
  }

  FsFile file;
  if (!Storage.openFileForRead("TXT", filepath, file)) {
    return false;
  }

  if (!file.seek(offset)) {
    return false;
  }

  size_t bytesRead = file.read(buffer, length);
  return bytesRead > 0;
}

Txt::ReadWindow::ReadWindow(const Txt& source)
    : txt(source), capacity(std::min(CAPACITY, source.fileSize)) {
  buffer = static_cast<uint8_t*>(malloc(capacity + 1));
}

Txt::ReadWindow::~ReadWindow() {
  (void)close();
  free(buffer);
}

bool Txt::ReadWindow::close() {
  const bool coherent = !failed && (!file || (!file.getError() && Storage.unchanged(stamp)));
  available = 0;
  failed = true;
  const bool closed = file.close();
  return coherent && closed;
}

const uint8_t* Txt::ReadWindow::read(size_t offset, size_t length) {
  if (failed || !buffer || !txt.loaded || length == 0 || length > capacity ||
      offset > txt.fileSize || length > txt.fileSize - offset) {
    return nullptr;
  }
  // Never reuse bytes across an observed write/remount/external-storage window.
  // Stop this operation instead of producing a mixed-generation page index.
  if (file && !Storage.unchanged(stamp)) {
    (void)close();
    return nullptr;
  }
  if (!file) {
    stamp = Storage.generation();
    if (!stamp.quiescent || !Storage.openFileForRead("TXT", txt.filepath, file) ||
        !file.seek(offset) || !Storage.unchanged(stamp)) {
      (void)close();
      return nullptr;
    }
    start = offset;
    cursor = offset;
  }

  size_t retained = 0;
  if (offset >= start && offset - start <= available) {
    retained = std::min(length, available - (offset - start));
    memmove(buffer, buffer + (offset - start), retained);
  }
  // Usually the retained suffix ends exactly at the current file position.
  // Explicit cursor accounting also makes shorter/backward reads exact.
  if (retained < length && cursor != offset + retained) {
    if (!file.seek(offset + retained)) {
      (void)close();
      return nullptr;
    }
    cursor = offset + retained;
  }
  const size_t needed = length - retained;
  if ((needed && file.read(buffer + retained, needed) != static_cast<int>(needed)) ||
      file.getError() || !Storage.unchanged(stamp)) {
    (void)close();
    return nullptr;
  }
  cursor += needed;
  start = offset;
  available = length;
  buffer[length] = '\0';
  return buffer;
}
