#pragma once

#include <RiscInputNavigationV1.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Bounded, read-only X4 TXT UI state. Storage owns the only file handle; this
// class owns filenames and one small, sanitized page, never file contents.
class X4TxtSession {
 public:
  static constexpr size_t kMaxFiles = 12;
  static constexpr size_t kMaxBytes = 128 * 1024;
  static constexpr size_t kPageBytes = 160;
  enum class View { List, Page };
  enum class Action { None, Redraw, Home };

  class Source {
   public:
    virtual ~Source() = default;
    virtual bool available() const = 0;
    virtual bool list(const char* directory, std::vector<std::string>& names) = 0;
    virtual bool open(const std::string& path, size_t& bytes) = 0;
    virtual bool readAt(size_t offset, uint8_t* output, size_t count) = 0;
    virtual bool close() = 0;
  };

  void enter(Source& source) {
    (void)source.close();
    files_.clear();
    selection_ = 0;
    view_ = View::List;
    bytes_ = 0;
    offset_ = 0;
    page_.clear();
    status_.clear();
    if (!source.available()) {
      status_ = "SD unavailable";
      return;
    }
    for (const char* directory : {"/Books", "/"}) {
      std::vector<std::string> names;
      if (!source.list(directory, names) || !source.available()) {
        files_.clear();
        status_ = "SD listing failed";
        return;
      }
      std::sort(names.begin(), names.end());
      for (const auto& name : names) {
        if (files_.size() >= kMaxFiles) break;
        if (safeTxtName(name))
          files_.push_back(std::string(directory) + (directory[1] ? "/" : "") + name);
      }
      if (files_.size() >= kMaxFiles) break;
    }
    if (files_.empty()) status_ = "No TXT files on SD";
  }

  void leave(Source& source) {
    (void)source.close();
    view_ = View::List;
    page_.clear();
    bytes_ = offset_ = 0;
  }

  Action input(uint32_t pressed, uint32_t released, Source& source) {
    const uint32_t directions = pressed & (RISC_NAV_LEFT | RISC_NAV_RIGHT);
    if (directions == (RISC_NAV_LEFT | RISC_NAV_RIGHT)) return Action::None;
    if (view_ == View::List) {
      if (directions) {
        const size_t count = files_.size() + 1;
        selection_ = directions == RISC_NAV_RIGHT
            ? (selection_ + 1) % count : (selection_ + count - 1) % count;
        if (!files_.empty()) status_.clear();
        return Action::Redraw;
      }
      if (pressed & RISC_NAV_BACK) return Action::Home;
      if (!(released & RISC_NAV_CONFIRM)) return Action::None;
      if (selection_ == 0) return Action::Home;
      size_t length = 0;
      if (!source.available() || !source.open(files_[selection_ - 1], length) || length > kMaxBytes) {
        (void)source.close();
        status_ = "TXT unavailable";
        return Action::Redraw;
      }
      bytes_ = length;
      view_ = View::Page;
      if (!loadPage(0, source)) return Action::Redraw;
      return Action::Redraw;
    }
    if ((released & RISC_NAV_CONFIRM) || (pressed & RISC_NAV_BACK)) {
      const bool closed = source.close();
      view_ = View::List;
      page_.clear();
      bytes_ = offset_ = 0;
      status_ = closed ? "" : "TXT close failed";
      return Action::Redraw;
    }
    if (!directions) return Action::None;
    size_t next = offset_;
    if (directions == RISC_NAV_RIGHT && bytes_ - offset_ > kPageBytes)
      next += kPageBytes;
    else if (directions == RISC_NAV_LEFT && offset_ >= kPageBytes)
      next -= kPageBytes;
    if (next == offset_) return Action::None;
    (void)loadPage(next, source);
    return Action::Redraw;
  }

  View view() const { return view_; }
  size_t selection() const { return selection_; }
  const std::vector<std::string>& files() const { return files_; }
  const std::string& page() const { return page_; }
  const std::string& status() const { return status_; }
  size_t offset() const { return offset_; }
  size_t bytes() const { return bytes_; }

 private:
  static bool safeTxtName(const std::string& name) {
    const auto dot = name.rfind('.');
    if (dot == std::string::npos || dot < 1 || dot > 8 || name.size() != dot + 4) return false;
    for (size_t i = 0; i < name.size(); ++i) {
      const unsigned char ch = static_cast<unsigned char>(name[i]);
      if (i == dot) continue;
      if (!(std::isalnum(ch) || ch == '_' || ch == '-' || ch == '~')) return false;
    }
    return std::toupper(static_cast<unsigned char>(name[dot + 1])) == 'T' &&
           std::toupper(static_cast<unsigned char>(name[dot + 2])) == 'X' &&
           std::toupper(static_cast<unsigned char>(name[dot + 3])) == 'T';
  }

  bool loadPage(size_t offset, Source& source) {
    offset_ = offset;
    page_.clear();
    status_.clear();
    if (bytes_ == 0) {
      status_ = "Empty TXT";
      return true;
    }
    const size_t count = std::min(kPageBytes, bytes_ - offset);
    uint8_t buffer[kPageBytes] = {};
    if (!source.available() || !source.readAt(offset, buffer, count)) {
      (void)source.close();
      view_ = View::List;
      bytes_ = offset_ = 0;
      status_ = "TXT read failed";
      return false;
    }
    page_.reserve(count);
    for (size_t i = 0; i < count; ++i)
      page_.push_back(buffer[i] >= 32 && buffer[i] <= 126 ? static_cast<char>(buffer[i]) : ' ');
    return true;
  }

  std::vector<std::string> files_;
  std::string page_;
  std::string status_;
  View view_ = View::List;
  size_t selection_ = 0;
  size_t bytes_ = 0;
  size_t offset_ = 0;
};
