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
  explicit OrdinarySdTreeOps(const char* root) : root_(root), started_(now()) {}
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
    if (!checkpoint()) return false;
    HalFile directory = Storage.open(path(relative).c_str(), O_RDONLY);
    if (!directory.isOpen()) return false;
    if (!directory.isDirectory()) { (void)directory.close(); return false; }
    bool good = true;
    size_t count = 0;
    while (good && checkpoint()) {
      HalFile entry = directory.openNextFile();
      if (!entry.isOpen()) { good = directory.getError() == 0; break; }
      char name[128]{};
      const size_t length = entry.getName(name, sizeof(name));
      good = ++count <= kMaxPackageEntries * kPackageResourceDepth + 1 &&
          length && length < sizeof(name) && visitor(name, entry.isDirectory());
      if (!entry.close()) good = false;
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
};
} // namespace RuntimePackages
