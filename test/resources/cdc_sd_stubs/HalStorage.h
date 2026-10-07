#pragma once
#include <fcntl.h>
#include "../../../lib/hal/StorageGeneration.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
namespace CdcSdTest {
inline std::string root, failClose, failWrite, failDirectoryRead;
inline int cut = 0, renames = 0;
inline uint64_t epoch=1,mount=1;
inline bool mutateDuringRead=false;
inline bool cacheable=false;
inline size_t directoryOpens=0,entryReads=0;
inline std::string full(const char* path) { return root + path; }
}  // namespace CdcSdTest
class HalFile {
  uint8_t metadataError_=0;
  std::fstream file_;
  std::string path_;
  bool directory_ = false, open_ = false;
  std::vector<std::string> children_;
  size_t next_ = 0;

 public:
  HalFile() = default;
  HalFile(const char* path, int flags) : path_(path) {
    auto full = CdcSdTest::full(path);
    if (std::filesystem::is_directory(full)) {
      ++CdcSdTest::directoryOpens;
      directory_ = true;
      open_ = true;
      for (auto const& child : std::filesystem::directory_iterator(full))
        children_.push_back(std::string(path) + (std::string(path) == "/" ? "" : "/") +
                            child.path().filename().string());
      return;
    }
    if ((flags & O_EXCL) && std::filesystem::exists(full)) return;
    if ((flags & O_WRONLY) && !(flags & O_CREAT) && !std::filesystem::exists(full)) return;
    file_.open(full, std::ios::binary | ((flags & O_WRONLY) ? std::ios::out : std::ios::in));
    open_ = file_.is_open();
  }
  HalFile(HalFile&&) = default;
  HalFile& operator=(HalFile&&) = default;
  struct DirectoryEntry { char name[128]{}; uint64_t size=0; bool isDirectory=false; };
  bool readDirectoryEntry(DirectoryEntry& result) {
    result={}; auto child=openNextFile();
    if(!child.isOpen()) return false;
    const size_t n=child.getName(result.name,sizeof(result.name));
    result.isDirectory=child.isDirectory();
    result.size=result.isDirectory?0:child.fileSize64();
    const bool closed=child.close();
    if(!n||n>=sizeof(result.name)||!closed){metadataError_=1;return false;}
    return true;
  }
  HalFile openNextFile() {
    ++CdcSdTest::entryReads;
    if(CdcSdTest::mutateDuringRead)++CdcSdTest::epoch;
    if (path_ == CdcSdTest::failDirectoryRead || next_ == children_.size()) return {};
    return HalFile(children_[next_++].c_str(), O_RDONLY);
  }
  uint8_t getError() const { return metadataError_?metadataError_:path_ == CdcSdTest::failDirectoryRead ? 1 : 0; }
  size_t getName(char* out, size_t capacity) const {
    const auto name = std::filesystem::path(path_).filename().string();
    if (name.size() >= capacity) return capacity;
    std::memcpy(out, name.c_str(), name.size() + 1);
    return name.size();
  }
  bool isOpen() const { return open_; }
  bool isDirectory() const { return directory_; }
  uint64_t fileSize64() const { return std::filesystem::file_size(CdcSdTest::full(path_.c_str())); }
  int read(char* out, size_t size) {
    file_.read(out, size);
    return static_cast<int>(file_.gcount());
  }
  size_t write(const char* bytes, size_t size) {
    const auto written = path_ == CdcSdTest::failWrite ? size / 2 : size;
    file_.write(bytes, written);
    return file_ ? written : 0;
  }
  bool close() {
    if (!open_) return false;
    open_ = false;
    if (file_.is_open()) file_.close();
    return path_ != CdcSdTest::failClose;
  }
};
struct CdcStorage {
  bool ready() const { return true; }
  StorageGenerationStamp generation() const { return {CdcSdTest::mount,CdcSdTest::epoch,CdcSdTest::cacheable}; }
  void invalidateObservations() { ++CdcSdTest::epoch; }
  bool exists(const char* path) const { return std::filesystem::exists(CdcSdTest::full(path)); }
  HalFile open(const char* path, int flags) const { return HalFile(path, flags); }
  bool rename(const char* from, const char* to) const {
    if (!exists(from) || exists(to)) return false;
    std::filesystem::rename(CdcSdTest::full(from), CdcSdTest::full(to));
    if (++CdcSdTest::renames == CdcSdTest::cut) throw 1;
    return true;
  }
  bool remove(const char* path) const { return std::filesystem::remove(CdcSdTest::full(path)); }
  bool openFileForRead(const char*, const char* path, HalFile& file) const {
    if (file.isOpen() && !file.close()) return false;
    file = const_cast<CdcStorage*>(this)->open(path, O_RDONLY);
    return file.isOpen() && !file.isDirectory();
  }

};
inline CdcStorage Storage;
