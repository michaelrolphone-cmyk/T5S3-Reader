#pragma once

#include <Print.h>
#include "StorageGeneration.h"
#include <common/FsApiConstants.h>  // for oflag_t
#include <freertos/semphr.h>

#include <memory>
#include <string>
#include <vector>
#if defined(BOARD_XTEINK_X4_PRO) || defined(BOARD_T5S3_PRO)
#include "RiscStorageVolumeV1.h"
#endif

class HalFile;
class HalReadBudget;

class HalStorage {
 public:
  HalStorage();
  bool begin();
#if defined(BOARD_XTEINK_X4_PRO) || defined(BOARD_T5S3_PRO)
  // Borrowed from the platform boot owner; it retains the provider module for the
  // whole Reader session. No SPI transport or filesystem implementation here.
  bool bindVolume(const risc_storage_volume_api_v1* volume);
#endif
  bool ready() const;
  StorageGenerationStamp generation() const;
  bool unchanged(const StorageGenerationStamp& stamp) const;
  // Explicit integrity boundaries retire cached observations even when no
  // managed write was observed. No media/reset/handle ownership change.
  void invalidateObservations();
  // Trusted compatibility boundary, not exported to applications. Actual raw
  // storage imports hold an uncertainty window through module teardown.
  void externalStorageBegin();
  void externalStorageEnd(bool closed);
  void externalStorageUncertain();
  // Refresh SdFat after raw SDFS access; never closes live handles or clears
  // failed/retained teardown uncertainty. A retained inventory may request it.
  bool reconcileExternalStorage();
  // Known media/power transitions mark unavailable without resetting
  // an active filesystem or closing another owner's handles.
  void markUnavailable();
  bool prepareForSleep();
  bool cancelSleep();
  bool commitSleep();
  std::vector<String> listFiles(const char* path = "/", int maxFiles = 200);
  // Read the entire file at `path` into a String. Returns empty string on failure.
  String readFile(const char* path);
  // Low-memory helpers:
  // Stream the file contents to a `Print` (e.g. `Serial`, or any `Print`-derived object).
  // Returns true on success, false on failure.
  bool readFileToStream(const char* path, Print& out, size_t chunkSize = 256);
  // Read up to `bufferSize-1` bytes into `buffer`, null-terminating it. Returns bytes read.
  size_t readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes = 0);
  // Write a string to `path` on the SD card. Overwrites existing file.
  // Returns true on success.
  bool writeFile(const char* path, const String& content);
  // Ensure a directory exists, creating it if necessary. Returns true on success.
  bool ensureDirectoryExists(const char* path);

  HalFile open(const char* path, const oflag_t oflag = O_RDONLY);
  bool mkdir(const char* path, const bool pFlag = true);
  bool exists(const char* path);
  bool remove(const char* path);
  bool rename(const char* oldPath, const char* newPath);
  bool rmdir(const char* path);

  bool openFileForRead(const char* moduleName, const char* path, HalFile& file);
  bool openFileForRead(const char* moduleName, const std::string& path, HalFile& file);
  bool openFileForRead(const char* moduleName, const String& path, HalFile& file);
  bool openFileForWrite(const char* moduleName, const char* path, HalFile& file);
  bool openFileForWrite(const char* moduleName, const std::string& path, HalFile& file);
  bool openFileForWrite(const char* moduleName, const String& path, HalFile& file);
  bool removeDir(const char* path);

  static HalStorage& getInstance() { return instance; }

  class StorageLock;  // private class, used internally

 private:
  static HalStorage instance;

  bool initialized = false;
  SemaphoreHandle_t storageMutex = nullptr;
};

#define Storage HalStorage::getInstance()

class HalFile : public Print {
  friend class HalStorage;
  class Impl;
  std::unique_ptr<Impl> impl;
  explicit HalFile(std::unique_ptr<Impl> impl);
  int readWithBudget(void* buf, size_t count, HalReadBudget* budget);

 public:
  HalFile();
  ~HalFile();
  HalFile(HalFile&&);
  HalFile& operator=(HalFile&&);
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;

  void flush();
  size_t getName(char* name, size_t len);
  size_t size();
  size_t fileSize();
  uint64_t fileSize64();
  bool seek(size_t pos);
  bool seek64(uint64_t pos);
  bool seekCur(int64_t offset);
  bool seekSet(size_t offset);
  int available() const;
  size_t position() const;
  int read(void* buf, size_t count);
  // Same I/O/error contract as read(); the caller owns the budget for this
  // operation and checkpoints any CPU work between reads. No read-ahead/state.
  int readCooperatively(void* buf, size_t count, HalReadBudget& budget);
  int read();  // read a single byte
  size_t write(const void* buf, size_t count);
  size_t write(uint8_t b) override;
  bool rename(const char* newPath);
  bool isDirectory() const;
  void rewindDirectory();
  bool close();
  // Metadata cursor: volume backends avoid child opens; legacy SdFat uses
  // its checked open/close cursor. No child handle escapes this method.
  // false is clean EOF only when getError() is zero. A failure is sticky.
  struct DirectoryEntry {
    char name[128]{};
    uint64_t size = 0;
    bool isDirectory = false;
  };
  bool readDirectoryEntry(DirectoryEntry& entry);
  HalFile openNextFile();
  // SdFat directory read errors must not be mistaken for clean enumeration end.
  uint8_t getError() const;
  bool isOpen() const;
  operator bool() const;
};

// Only do renaming FsFile to HalFile if this header is included by downstream code
// The renaming is to allow using the thread-safe HalFile instead of the raw FsFile, without needing to change the
// downstream code
#ifndef HAL_STORAGE_IMPL
using FsFile = HalFile;
#endif

// Downstream code must use Storage instead of SdMan
#ifdef SdMan
#undef SdMan
#endif
