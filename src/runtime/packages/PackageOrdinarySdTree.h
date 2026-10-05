#pragma once
#include "PackageOrdinaryTree.h"
#include <HalStorage.h>
#include <string>
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#else
#include <chrono>
#endif

namespace RuntimePackages {
class OrdinarySdTreeOps {
 public:
  explicit OrdinarySdTreeOps(const char* root, const OrdinaryPackagePlan* plan = nullptr,
                            uint64_t* sizes = nullptr, OrdinaryInspectionDiagnostic* diagnostic = nullptr)
      : root_(root), started_(now()), plan_(plan), sizes_(sizes), diagnostic_(diagnostic) {}
  bool checkpoint() {
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
    vTaskDelay(1);
#endif
    return now() - started_ <= 10000000u;
  }
  bool exists(const char* relative) { return Storage.exists(path(relative).c_str()); }
  bool remove(const char* relative) {
    return checkpoint() && Storage.remove(path(relative).c_str());
  }
  bool rmdir(const char* relative) {
    return checkpoint() && Storage.rmdir(path(relative).c_str());
  }
  template<class Visitor>
  bool visit(const char* relative, Visitor visitor) {
    if (!checkpoint() || (sizes_ && !plan_)) return false;
    HalFile directory = Storage.open(path(relative).c_str(), O_RDONLY);
    if (!directory.isOpen()) return false;
    if (!directory.isDirectory()) { (void)directory.close(); return false; }
    bool good = true;
    while (good && checkpoint()) {
      HalFile::DirectoryEntry entry;
      if (!directory.readDirectoryEntry(entry)) { good = directory.getError() == 0; break; }
      const size_t length = std::strlen(entry.name);
      // Enumeration is bounded by the cooperative ten-second deadline rather
      // than the number of unrelated directory entries.
      good = length && length < sizeof(entry.name) && visitor(entry.name, entry.isDirectory);
      if (good && sizes_ && !entry.isDirectory) {
        const std::string name = relative[0] ? std::string(relative) + "/" + entry.name : entry.name;
        for (size_t i = 0; i < plan_->entryCount; ++i) {
          if (name != plan_->entries[i].name) continue;
          if (entry.size != plan_->entries[i].sizeBytes) good = false;
          sizes_[i] = entry.size;
          break;
        }
      }
      if (!good && diagnostic_) diagnostic_->fail("tree-entry", entry.name);
    }
    if (!checkpoint()) good = false;
    return directory.close() && good;
  }
  bool createParents(const char* relative) {
    if (!safePackageResourcePath(relative)) return false;
    const std::string name(relative);
    for (size_t i = 0; i < name.size(); ++i) {
      if (name[i] != '/') continue;
      if (!checkpoint()) return false;
      const std::string parent = path(name.substr(0, i).c_str());
      if (!Storage.exists(parent.c_str()) && !Storage.mkdir(parent.c_str(), false)) return false;
      HalFile directory = Storage.open(parent.c_str(), O_RDONLY);
      if (!directory.isOpen()) return false;
      const bool valid = directory.isDirectory();
      if (!directory.close() || !valid) return false;
    }
    return true;
  }
 private:
  std::string path(const char* relative) const {
    return relative[0] ? root_ + "/" + relative : root_;
  }
  static uint64_t now() {
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
    return static_cast<uint64_t>(esp_timer_get_time());
#else
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
  }
  std::string root_;
  uint64_t started_;
  const OrdinaryPackagePlan* plan_;
  uint64_t* sizes_;
  OrdinaryInspectionDiagnostic* diagnostic_;
};
} // namespace RuntimePackages
