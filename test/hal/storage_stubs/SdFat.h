#pragma once
#include <Print.h>
#include <common/FsApiConstants.h>
#include <freertos/semphr.h>

#include <cassert>
#include <cstring>
#include <map>
#include <memory>
#include <vector>
namespace FakeSd {
struct Node {
  bool directory = false;
  std::string bytes;
};
inline std::map<std::string, std::shared_ptr<Node>> nodes;
inline bool mountOkay = true;
inline unsigned mounts = 0, closedWrites = 0, opens = 0, reads = 0;
inline uint64_t bytesRead = 0;
inline std::string failWrite, failRead, failClose, failDirectory, failOpen, failSeek, failName;
inline unsigned mediaError = 0;
inline void (*afterClose)(const std::string&) = nullptr;
inline void (*afterRead)(const std::string&, size_t) = nullptr;
inline std::string path(const char* name) {
  std::string input = name ? name : "";
  if (input.empty() || input[0] != '/') input = '/' + input;
  std::vector<std::string> parts;
  std::string item;
  for (size_t i = 1; i <= input.size(); ++i) {
    char c = i == input.size() ? '/' : input[i];
    if (c == '/') {
      if (item == "..") {
        if (!parts.empty()) parts.pop_back();
      } else if (!item.empty() && item != ".")
        parts.push_back(item);
      item.clear();
    } else
      item += c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
  }
  std::string out;
  for (const auto& p : parts) out += '/' + p;
  return out.empty() ? "/" : out;
}
inline void locked() { assert(FakeLock::held); }
inline bool rename(const std::string& from, const std::string& to) {
  locked();
  if (!nodes.count(from) || nodes.count(to)) return false;
  std::vector<std::pair<std::string, std::shared_ptr<Node>>> moved;
  for (auto it = nodes.begin(); it != nodes.end();) {
    if (it->first == from || it->first.compare(0, from.size() + 1, from + "/") == 0) {
      moved.push_back({to + it->first.substr(from.size()), it->second});
      it = nodes.erase(it);
    } else
      ++it;
  }
  for (auto& entry : moved) nodes.emplace(entry);
  return true;
}
}  // namespace FakeSd
class FsFile {
  std::shared_ptr<FakeSd::Node> node_;
  std::string path_;
  bool write_ = false;
  size_t position_ = 0, next_ = 0;
  uint8_t error_ = 0;

 public:
  FsFile() = default;
  explicit FsFile(const char* name, int flags = O_RDONLY) {
    FakeSd::locked();
    ++FakeSd::opens;
    path_ = FakeSd::path(name);
    if (path_ == FakeSd::failOpen) return;
    auto found = FakeSd::nodes.find(path_);
    if (found != FakeSd::nodes.end() && (flags & O_EXCL)) return;
    if (found == FakeSd::nodes.end()) {
      if (!(flags & O_CREAT)) return;
      const auto parent = path_.substr(0, path_.find_last_of('/'));
      auto dir = FakeSd::nodes.find(parent.empty() ? "/" : parent);
      if (dir == FakeSd::nodes.end() || !dir->second->directory) return;
      found = FakeSd::nodes.emplace(path_, std::make_shared<FakeSd::Node>()).first;
    }
    node_ = found->second;
    write_ = (flags & (O_WRONLY | O_RDWR)) != 0;
    if (flags & O_TRUNC) node_->bytes.clear();
    if (flags & O_APPEND) position_ = node_->bytes.size();
  }
  FsFile(const FsFile&) = delete;
  FsFile& operator=(const FsFile&) = delete;
  FsFile(FsFile&&) = default;
  FsFile& operator=(FsFile&&) = default;
  ~FsFile() {
    if (node_) {
      FakeSd::locked();
      (void)close();
    }
  }
  bool isOpen() const {
    FakeSd::locked();
    return bool(node_);
  }
  explicit operator bool() const { return isOpen(); }
  bool isDirectory() const {
    FakeSd::locked();
    return node_ && node_->directory;
  }
  bool close() {
    FakeSd::locked();
    if (!node_) return true;
    if (path_ == FakeSd::failClose) {
      error_ = 1;
      return false;
    }
    if (write_) ++FakeSd::closedWrites;
    node_.reset();
    if (FakeSd::afterClose) FakeSd::afterClose(path_);
    return true;
  }
  uint8_t getError() const {
    FakeSd::locked();
    return error_;
  }
  size_t getName(char* out, size_t capacity) {
    FakeSd::locked();
    if (path_ == FakeSd::failName) { error_ = 1; return 0; }
    auto name = path_.substr(path_.find_last_of('/') + 1);
    if (name.size() >= capacity) return capacity;
    std::memcpy(out, name.c_str(), name.size() + 1);
    return name.size();
  }
  size_t size() const {
    FakeSd::locked();
    return node_ ? node_->bytes.size() : 0;
  }
  size_t fileSize() const { return size(); }
  uint64_t fileSize64() const { return size(); }
  size_t position() const {
    FakeSd::locked();
    return position_;
  }
  bool seekSet(uint64_t offset) {
    FakeSd::locked();
    if (path_ == FakeSd::failSeek) { error_ = 1; return false; }
    if (!node_ || offset > node_->bytes.size()) return false;
    position_ = offset;
    return true;
  }
  bool seekCur(int64_t offset) { return offset >= -static_cast<int64_t>(position_) && seekSet(position_ + offset); }
  int available() const {
    FakeSd::locked();
    return node_ ? static_cast<int>(node_->bytes.size() - position_) : 0;
  }
  int read(void* out, size_t count) {
    FakeSd::locked();
    ++FakeSd::reads;
    if (path_ == FakeSd::failRead) {
      error_ = 1;
      return -1;
    }
    if (!node_) return -1;
    count = std::min(count, node_->bytes.size() - position_);
    std::memcpy(out, node_->bytes.data() + position_, count);
    position_ += count;
    FakeSd::bytesRead += count;
    if (FakeSd::afterRead) FakeSd::afterRead(path_, count);
    return static_cast<int>(count);
  }
  int read() {
    uint8_t value = 0;
    return read(&value, 1) == 1 ? value : -1;
  }
  size_t write(const void* bytes, size_t count) {
    FakeSd::locked();
    if (!node_ || !write_) return 0;
    if (path_ == FakeSd::failWrite) {
      count /= 2;
      error_ = 1;
    }
    if (position_ + count > node_->bytes.size()) node_->bytes.resize(position_ + count);
    std::memcpy(&node_->bytes[position_], bytes, count);
    position_ += count;
    return count;
  }
  size_t write(uint8_t value) { return write(&value, 1); }
  size_t print(const String& text) { return write(text.data(), text.size()); }
  bool sync() {
    FakeSd::locked();
    if (path_ == FakeSd::failWrite) {
      error_ = 1;
      return false;
    }
    return true;
  }
  void flush() { (void)sync(); }
  bool rename(const char* to) {
    if (!FakeSd::rename(path_, FakeSd::path(to))) return false;
    path_ = FakeSd::path(to);
    return true;
  }
  void rewindDirectory() {
    FakeSd::locked();
    next_ = 0;
  }
  FsFile openNextFile() {
    FakeSd::locked();
    if (path_ == FakeSd::failDirectory) {
      error_ = 1;
      return {};
    }
    std::vector<std::string> children;
    const std::string prefix = path_ == "/" ? "/" : path_ + "/";
    for (const auto& item : FakeSd::nodes) {
      if (item.first.compare(0, prefix.size(), prefix) == 0 && item.first.size() > prefix.size() &&
          item.first.find('/', prefix.size()) == std::string::npos)
        children.push_back(item.first);
    }
    if (next_ == children.size()) return {};
    return FsFile(children[next_++].c_str());
  }
};
class SdFat {
 public:
  bool begin(int, uint32_t) {
    FakeSd::locked();
    ++FakeSd::mounts;
    FakeSd::nodes.try_emplace("/", std::make_shared<FakeSd::Node>(FakeSd::Node{true, {}}));
    return FakeSd::mountOkay;
  }
  FsFile open(const char* path, int flags = O_RDONLY) { return FsFile(path, flags); }
  FsFile open(const String& path, int flags = O_RDONLY) { return open(path.c_str(), flags); }
  bool exists(const char* path) {
    FakeSd::locked();
    return FakeSd::nodes.count(FakeSd::path(path));
  }
  bool exists(const String& path) { return exists(path.c_str()); }
  bool mkdir(const char* path, bool recursive = true) {
    FakeSd::locked();
    std::string name = FakeSd::path(path);
    if (FakeSd::nodes.count(name)) return false;
    if (recursive) {
      for (size_t pos = 1; (pos = name.find('/', pos)) != std::string::npos; ++pos)
        FakeSd::nodes.try_emplace(name.substr(0, pos), std::make_shared<FakeSd::Node>(FakeSd::Node{true, {}}));
    }
    FakeSd::nodes[name] = std::make_shared<FakeSd::Node>(FakeSd::Node{true, {}});
    return true;
  }
  bool remove(const char* path) {
    FakeSd::locked();
    return FakeSd::nodes.erase(FakeSd::path(path));
  }
  bool remove(const String& path) { return remove(path.c_str()); }
  bool rmdir(const char* path) { return remove(path); }
  bool rename(const char* from, const char* to) { return FakeSd::rename(FakeSd::path(from), FakeSd::path(to)); }
  unsigned sdErrorCode() { return FakeSd::mediaError; }
  unsigned sdErrorData() { return 0; }
};
