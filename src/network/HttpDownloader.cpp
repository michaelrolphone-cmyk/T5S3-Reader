#include "HttpDownloader.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Logging.h>
#if __has_include(<NetworkClient.h>)
#include <NetworkClient.h>
#include <NetworkClientSecure.h>
using CrossPointHttpClient = NetworkClient;
using CrossPointHttpClientSecure = NetworkClientSecure;
#else
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
using CrossPointHttpClient = WiFiClient;
using CrossPointHttpClientSecure = WiFiClientSecure;
#endif
#include <base64.h>

#include <cstring>
#include <memory>
#include <utility>

#include "runtime/network/NetworkService.h"
#include "util/UrlUtils.h"

namespace {
constexpr uint32_t kNetworkReadyTimeoutMs = 5000;

bool waitForNetworkReady() {
  if (RuntimeNetwork::ready()) return true;
  const uint32_t started = millis();
  while (millis() - started < kNetworkReadyTimeoutMs) {
    if (RuntimeNetwork::ready()) return true;
    delay(50);
  }
  return RuntimeNetwork::ready();
}

class StringWriteStream final : public Stream {
 public:
  explicit StringWriteStream(std::string& output) : output_(output) { output_.clear(); }

  size_t write(uint8_t byte) override { return write(&byte, 1); }

  size_t write(const uint8_t* buffer, size_t size) override {
    if (!buffer || size == 0) return 0;
    output_.append(reinterpret_cast<const char*>(buffer), size);
    return size;
  }

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

 private:
  std::string& output_;
};

class FileWriteStream final : public Stream {
 public:
  FileWriteStream(FsFile& file, size_t total, HttpDownloader::ProgressCallback progress)
      : file_(file), total_(total), progress_(std::move(progress)) {}

  size_t write(uint8_t byte) override { return write(&byte, 1); }

  size_t write(const uint8_t* buffer, size_t size) override {
    // Write-through stream for HTTPClient::writeToStream with progress tracking.
    const size_t written = file_.write(buffer, size);
    if (written != size) {
      writeOk_ = false;
    }
    downloaded_ += written;
    if (progress_ && total_ > 0) {
      progress_(downloaded_, total_);
    }
    return written;
  }

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override { file_.flush(); }

  size_t downloaded() const { return downloaded_; }
  bool ok() const { return writeOk_; }

 private:
  FsFile& file_;
  size_t total_;
  size_t downloaded_ = 0;
  bool writeOk_ = true;
  HttpDownloader::ProgressCallback progress_;
};
}  // namespace

bool HttpDownloader::fetchUrl(const std::string& url, Stream& outContent, const std::string& username,
                              const std::string& password) {
  if (!waitForNetworkReady()) {
    LOG_ERR("HTTP", "Network is not ready (no usable IP address)");
    return false;
  }

  std::unique_ptr<CrossPointHttpClient> client;
  if (UrlUtils::isHttpsUrl(url)) {
    auto* secureClient = new CrossPointHttpClientSecure();
    secureClient->setInsecure();
    client.reset(secureClient);
  } else {
    client.reset(new CrossPointHttpClient());
  }
  HTTPClient http;

  LOG_DBG("HTTP", "Fetching: %s", url.c_str());

  http.begin(*client, url.c_str());
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);

  if (!username.empty() && !password.empty()) {
    std::string credentials = username + ":" + password;
    String encoded = base64::encode(credentials.c_str());
    http.addHeader("Authorization", "Basic " + encoded);
  }

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    LOG_ERR("HTTP", "Fetch failed: %d", httpCode);
    http.end();
    return false;
  }

  const int writeResult = http.writeToStream(&outContent);
  http.end();
  if (writeResult < 0) {
    LOG_ERR("HTTP", "Fetch stream failed: %d", writeResult);
    return false;
  }

  LOG_DBG("HTTP", "Fetch success: %d bytes", writeResult);
  return true;
}

bool HttpDownloader::fetchUrl(const std::string& url, std::string& outContent, const std::string& username,
                              const std::string& password) {
  // Write directly into the caller's std::string. The old StreamString path
  // held a complete second copy of large responses before assigning them to
  // outContent; the GitHub release catalog is now large enough for that peak
  // allocation to exhaust the ESP32 heap.
  StringWriteStream stream(outContent);
  return fetchUrl(url, stream, username, password);
}

HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& destPath,
                                                             ProgressCallback progress, const std::string& username,
                                                             const std::string& password) {
  if (!waitForNetworkReady()) {
    LOG_ERR("HTTP", "Network is not ready (no usable IP address)");
    return HTTP_ERROR;
  }

  std::unique_ptr<CrossPointHttpClient> client;
  if (UrlUtils::isHttpsUrl(url)) {
    auto* secureClient = new CrossPointHttpClientSecure();
    secureClient->setInsecure();
    client.reset(secureClient);
  } else {
    client.reset(new CrossPointHttpClient());
  }
  HTTPClient http;

  LOG_DBG("HTTP", "Downloading: %s", url.c_str());
  LOG_DBG("HTTP", "Destination: %s", destPath.c_str());

  http.begin(*client, url.c_str());
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);

  if (!username.empty() && !password.empty()) {
    std::string credentials = username + ":" + password;
    String encoded = base64::encode(credentials.c_str());
    http.addHeader("Authorization", "Basic " + encoded);
  }

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    LOG_ERR("HTTP", "Download failed: %d", httpCode);
    http.end();
    return HTTP_ERROR;
  }

  const int64_t reportedLength = http.getSize();
  const size_t contentLength = reportedLength > 0 ? static_cast<size_t>(reportedLength) : 0;
  if (contentLength > 0) {
    LOG_DBG("HTTP", "Content-Length: %zu", contentLength);
  } else {
    LOG_DBG("HTTP", "Content-Length: unknown");
  }

  // Ensure the parent directory exists before opening the destination. This is
  // especially important for first-run native app installs to /Apps.
  const size_t separator = destPath.find_last_of('/');
  if (separator != std::string::npos && separator > 0) {
    const std::string parent = destPath.substr(0, separator);
    if (!Storage.ensureDirectoryExists(parent.c_str())) {
      LOG_ERR("HTTP", "Failed to create destination directory: %s", parent.c_str());
      http.end();
      return FILE_ERROR;
    }
  }

  // Remove existing file if present
  if (Storage.exists(destPath.c_str())) {
    Storage.remove(destPath.c_str());
  }

  // Open file for writing
  FsFile file;
  if (!Storage.openFileForWrite("HTTP", destPath.c_str(), file)) {
    LOG_ERR("HTTP", "Failed to open file for writing");
    http.end();
    return FILE_ERROR;
  }

  // Let HTTPClient handle chunked decoding and stream body bytes into the file.
  FileWriteStream fileStream(file, contentLength, progress);
  const int writeResult = http.writeToStream(&fileStream);

  file.close();
  http.end();

  if (writeResult < 0) {
    LOG_ERR("HTTP", "writeToStream error: %d", writeResult);
    Storage.remove(destPath.c_str());
    return HTTP_ERROR;
  }

  const size_t downloaded = fileStream.downloaded();
  LOG_DBG("HTTP", "Downloaded %zu bytes", downloaded);

  // Guard against partial writes even if HTTPClient completes.
  if (!fileStream.ok()) {
    LOG_ERR("HTTP", "Write failed during download");
    Storage.remove(destPath.c_str());
    return FILE_ERROR;
  }

  if (contentLength == 0 && downloaded == 0) {
    LOG_ERR("HTTP", "Download failed: no data received");
    Storage.remove(destPath.c_str());
    return HTTP_ERROR;
  }

  // Verify download size if known
  if (contentLength > 0 && downloaded != contentLength) {
    LOG_ERR("HTTP", "Size mismatch: got %zu, expected %zu", downloaded, contentLength);
    Storage.remove(destPath.c_str());
    return HTTP_ERROR;
  }

  return OK;
}