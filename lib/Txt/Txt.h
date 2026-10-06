#pragma once

#include <HalStorage.h>

#include <memory>
#include <string>

class Txt {
  std::string filepath;
  std::string cacheBasePath;
  std::string cachePath;
  bool loaded = false;
  size_t fileSize = 0;

 public:
  explicit Txt(std::string path, std::string cacheBasePath);

  bool load();
  [[nodiscard]] const std::string& getPath() const { return filepath; }
  [[nodiscard]] const std::string& getCachePath() const { return cachePath; }
  [[nodiscard]] std::string getTitle() const;
  [[nodiscard]] size_t getFileSize() const { return fileSize; }

  void setupCacheDir() const;

  // Cover image support - looks for cover.bmp/jpg/jpeg/png in same folder as txt file
  [[nodiscard]] std::string getCoverBmpPath() const;
  [[nodiscard]] bool generateCoverBmp() const;
  [[nodiscard]] std::string findCoverImage() const;

  // A single indexing operation owns its bounded read-ahead and file handle.
  // Random-access rendering keeps using readContent below.
  class ReadWindow {
    const Txt& txt;
    const size_t capacity;
    FsFile file;
    uint8_t* buffer = nullptr;
    StorageGenerationStamp stamp;
    size_t start = 0;
    size_t available = 0;
    size_t cursor = 0;
    bool failed = false;

   public:
    static constexpr size_t CAPACITY = 8 * 1024;
    explicit ReadWindow(const Txt& source);
    ~ReadWindow();
    ReadWindow(const ReadWindow&) = delete;
    ReadWindow& operator=(const ReadWindow&) = delete;
    // Returned bytes last until the next read. Failure never exposes a partial window.
    const uint8_t* read(size_t offset, size_t length);
    bool close();
  };

  // Read content from file
  [[nodiscard]] bool readContent(uint8_t* buffer, size_t offset, size_t length) const;
};
