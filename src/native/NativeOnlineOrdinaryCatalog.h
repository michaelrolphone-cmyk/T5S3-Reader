#pragma once

#include "NativeOnlineRtePackageInstall.h"
#include "WifiCredentialStore.h"
#include "network/HttpDownloader.h"
#include "runtime/network/NetworkService.h"
#include "runtime/packages/PackageOnlineCatalog.h"
#include "runtime/memory/PsramBuffer.h"
#include <Logging.h>

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <memory>
#include <new>
#include <string>
#include <cstring>

namespace RuntimeOnlinePackages {
namespace Catalog {

constexpr const char* kLatestCatalog =
    "https://github.com/michaelrolphone-cmyk/T5S3-Reader/releases/latest/download/package-catalog.json";
constexpr const char* kIndependentCatalog =
    "https://raw.githubusercontent.com/michaelrolphone-cmyk/T5S3-Reader/release-index/release-index.json";
constexpr uint32_t kConnectTimeoutMs = 15000;
constexpr uint32_t kRefreshTimeoutMs = 90000;

struct CatalogWork {
  uint32_t started = millis();
  uint32_t checkpoint = started;
  unsigned chunks = 0;
  bool step() {
    const uint32_t now = millis();
    if (now - started >= kRefreshTimeoutMs) return false;
    // Called on bounded network chunks and at each 1024 parser bytes/row.
    if (++chunks >= 4 || now - checkpoint >= 8) {
      esp_task_wdt_reset();
      vTaskDelay(1);
      checkpoint = millis();
      chunks = 0;
    }
    return true;
  }
  static bool cooperate(void* context) {
    return static_cast<CatalogWork*>(context)->step();
  }
};

// Fixed PSRAM allocation, independent byte budgets and one overall deadline.
// Short writes cancel HTTP intake; allocation/overflow/timeout fail closed.
class BoundedCatalogSink final : public Stream {
 public:
  explicit BoundedCatalogSink(CatalogWork& work,
      size_t limit = RuntimePackages::kCatalogMaxBytes)
      : work_(work), buffer_(limit + 1, false), limit_(limit) {}
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* data, size_t size) override {
    if (failed_ || !buffer_ || (!data && size) || size > limit_ - size_ || !work_.step()) {
      failed_ = true;
      return 0;
    }
    if (size) std::memcpy(buffer_.data() + size_, data, size);
    size_ += size;
    buffer_.data()[size_] = 0;
    return size;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  bool failed() const { return failed_ || !buffer_; }
  bool ready() const { return !failed() && size_; }
  const char* data() const { return buffer_ ? buffer_.chars() : nullptr; }
  size_t size() const { return size_; }
 private:
  CatalogWork& work_;
  RuntimeMemory::PsramBuffer buffer_;
  size_t limit_;
  size_t size_ = 0;
  bool failed_ = false;
};

inline bool connectSavedWifi() {
  if (RuntimeNetwork::ready()) return true;
  WIFI_STORE.loadFromFile();
  const WifiCredential* credential = nullptr;
  const std::string last = WIFI_STORE.getLastConnectedSsid();
  if (!last.empty()) credential = WIFI_STORE.findCredential(last);
  if (!credential) {
    const auto& saved = WIFI_STORE.getCredentials();
    if (!saved.empty()) credential = &saved.front();
  }
  if (!credential || credential->ssid.empty()) return false;
  RuntimeNetwork::wifi().connect(credential->ssid.c_str(),
      credential->password.empty() ? nullptr : credential->password.c_str());
  const uint32_t started = millis();
  while (millis() - started < kConnectTimeoutMs) {
    esp_task_wdt_reset();
    const auto state = RuntimeNetwork::state();
    if (state.connection == RuntimeNetwork::ConnectionState::Connected &&
        state.hasAddress) {
      WIFI_STORE.setLastConnectedSsid(credential->ssid);
      return true;
    }
    if (state.connection == RuntimeNetwork::ConnectionState::Failed ||
        state.connection == RuntimeNetwork::ConnectionState::NetworkNotFound) return false;
    delay(100);
  }
  return RuntimeNetwork::ready();
}

// Catalog objects are also bulk metadata. Keep their ~60/40 KiB arrays out
// of the internal heap needed by TLS and task stacks; no fallback on failure.
template<class T> struct CatalogDelete {
  void operator()(T* value) const {
    if (value) { value->~T(); heap_caps_free(value); }
  }
};
template<class T> using CatalogPtr = std::unique_ptr<T, CatalogDelete<T>>;
template<class T> CatalogPtr<T> allocateCatalog() {
  void* bytes = heap_caps_malloc(sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return CatalogPtr<T>(bytes ? new (bytes) T{} : nullptr);
}
inline CatalogPtr<RuntimePackages::OnlinePackageCatalog>& active() {
  static CatalogPtr<RuntimePackages::OnlinePackageCatalog> catalog;
  return catalog;
}

// Independent metadata is required, even when it contains only historical
// barriers. Never fall back to older aggregate versions on index failure.
// Aggregate HTTP absence is legitimate after independent releases; a fetched
// corrupt/oversized aggregate is not. Publish the combined snapshot only once.
inline bool refresh() {
  active().reset();
  if (!connectSavedWifi()) return false;
  CatalogWork work;
  auto independent = allocateCatalog<RuntimePackages::IndependentDriverCatalog>();
  if (!independent) return false;
  {
    LOG_INF("PACKAGES", "Reading independent package versions");
    BoundedCatalogSink response(work, RuntimePackages::kIndependentCatalogMaxBytes);
    if (!HttpDownloader::fetchUrl(kIndependentCatalog, response) || !response.ready() ||
        !RuntimePackages::parseIndependentDriverCatalog(response.data(), response.size(),
            *independent, CatalogWork::cooperate, &work)) return false;
  }
  CatalogPtr<RuntimePackages::PackageCatalog> aggregate;
  {
    LOG_INF("PACKAGES", "Reading aggregate package compatibility catalog");
    BoundedCatalogSink response(work);
    const bool fetched = HttpDownloader::fetchUrl(kLatestCatalog, response);
    if (response.failed() || (!fetched && response.size()) || !work.step()) return false;
    if (fetched) {
      aggregate = allocateCatalog<RuntimePackages::PackageCatalog>();
      if (!response.ready() || !aggregate || !RuntimePackages::parsePackageCatalog(
          response.data(), response.size(), *aggregate)) return false;
    }
  }
  auto next = allocateCatalog<RuntimePackages::OnlinePackageCatalog>();
  if (!next || !work.step() || !RuntimePackages::mergeOnlineCatalog(
      aggregate.get(), *independent, "xtensa-esp32s3", *next) || !work.step()) return false;
  LOG_INF("PACKAGES", "Selected %u immutable package archives", unsigned(next->packageCount));
  active() = std::move(next);
  return true;
}

inline bool permitted(const RuntimePackages::CatalogPackage& item, int allowedKind) {
  return allowedKind == 4 ||
         (allowedKind >= 0 && allowedKind <= 3 &&
          static_cast<int>(item.identity.kind) == allowedKind);
}
inline uint32_t count(int allowedKind) {
  const auto& catalog = active();
  if (!catalog || allowedKind < 0) return 0;
  uint32_t n = 0;
  for (size_t i = 0; i < catalog->packageCount; ++i)
    if (permitted(catalog->packages[i], allowedKind)) ++n;
  return n;
}
inline bool selected(int allowedKind, uint32_t index,
                     RuntimePackages::CatalogPackage& out,
                     char (&release)[RuntimePackages::kOnlineReleaseTagBytes]) {
  const auto& catalog = active();
  if (!catalog || allowedKind < 0) return false;
  for (size_t i = 0; i < catalog->packageCount; ++i) {
    if (!permitted(catalog->packages[i], allowedKind)) continue;
    if (!index--) {
      out = catalog->packages[i];
      std::memcpy(release, catalog->releases[i], sizeof(release));
      return true;
    }
  }
  return false;
}

} // namespace Catalog
} // namespace RuntimeOnlinePackages
