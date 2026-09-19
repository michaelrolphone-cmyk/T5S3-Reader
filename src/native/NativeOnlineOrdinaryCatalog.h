#pragma once

#include "NativeOnlineRtePackageInstall.h"
#include "WifiCredentialStore.h"
#include "network/HttpDownloader.h"
#include "runtime/network/NetworkService.h"
#include "runtime/packages/PackageCatalog.h"

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
constexpr uint32_t kConnectTimeoutMs = 15000;

// Do not allow HttpDownloader to accumulate a remote response in an unbounded
// std::string. A short write explicitly aborts the HTTP transfer.
class BoundedCatalogSink final : public Stream {
 public:
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* data, size_t size) override {
    if (failed_ || (!data && size) || size > RuntimePackages::kCatalogMaxBytes - body_.size()) {
      failed_ = true;
      return 0;
    }
    if (size) body_.append(reinterpret_cast<const char*>(data), size);
    return size;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  bool ready() const { return !failed_ && !body_.empty(); }
  const std::string& body() const { return body_; }
 private:
  std::string body_;
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

inline std::unique_ptr<RuntimePackages::PackageCatalog>& active() {
  static std::unique_ptr<RuntimePackages::PackageCatalog> catalog;
  return catalog;
}

// The catalog is the only network discovery document. Store its release tag
// alongside its entries; never use latest again to fetch a selected archive.
// An unsuccessful refresh invalidates any previous selection.
inline bool refresh() {
  active().reset();
  if (!connectSavedWifi()) return false;
  BoundedCatalogSink response;
  esp_task_wdt_reset();
  if (!HttpDownloader::fetchUrl(kLatestCatalog, response) || !response.ready())
    return false;
  std::unique_ptr<RuntimePackages::PackageCatalog> next(
      new (std::nothrow) RuntimePackages::PackageCatalog{});
  if (!next || !RuntimePackages::parsePackageCatalog(
          response.body().data(), response.body().size(), *next)) return false;
  if (!RuntimePackages::CatalogDetail::safeReleaseTag(next->release)) return false;
  for (size_t i = 0; i < next->packageCount; ++i) {
    const auto& pkg = next->packages[i];
    if (std::strcmp(pkg.architecture, "xtensa-esp32s3")) return false;
    std::string url;
    if (!OrdinaryZip::archiveUrl(pkg, next->release, url)) return false;
  }
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
                     char (&release)[64]) {
  const auto& catalog = active();
  if (!catalog || allowedKind < 0) return false;
  for (size_t i = 0; i < catalog->packageCount; ++i) {
    if (!permitted(catalog->packages[i], allowedKind)) continue;
    if (!index--) {
      out = catalog->packages[i];
      std::memcpy(release, catalog->release, sizeof(catalog->release));
      return true;
    }
  }
  return false;
}

} // namespace Catalog
} // namespace RuntimeOnlinePackages
