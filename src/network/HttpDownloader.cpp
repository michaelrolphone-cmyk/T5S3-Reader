#include "HttpDownloader.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Logging.h>
#include <T5StreamApi.h>
#include <esp_task_wdt.h>
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
#include <limits>
#include <memory>
#include <utility>

#include "runtime/network/NetworkService.h"
#include "runtime/streams/HttpStreamTransfer.h"
#include "runtime/streams/HttpUrlValidation.h"
#include "util/UrlUtils.h"

namespace {
constexpr uint32_t kNetworkReadyTimeoutMs = 5000;
constexpr size_t kNativeMetadataLimit = 64 * 1024;

bool waitForNetworkReady() {
  if (RuntimeNetwork::ready()) return true;
  const uint32_t started = millis();
  while (millis() - started < kNetworkReadyTimeoutMs) {
    if (RuntimeNetwork::ready()) return true;
    delay(50);
  }
  return RuntimeNetwork::ready();
}

// The native-app invocation is the only task authorized to request its stream
// API. The HTTP provider's own network task deliberately receives nullptr here
// and uses the existing HTTPClient implementation below, avoiding recursion.
const t5_stream_api_v1* invocationStreams(const std::string& username, const std::string& password) {
  if (!username.empty() || !password.empty()) return nullptr;  // No auth in open_http v1.
  return t5_stream_get_api(T5_STREAM_API_VERSION);
}

RuntimeHttpStreams::Hooks streamHooks() {
  RuntimeHttpStreams::Hooks hooks{};
  hooks.now_ms = [](void*) -> uint32_t { return millis(); };
  hooks.cooperate = [](void*) { esp_task_wdt_reset(); delay(5); };
  return hooks;
}

class StringWriteStream final : public Stream {
 public:
  explicit StringWriteStream(std::string& output, size_t limit = std::numeric_limits<size_t>::max())
      : output_(output), limit_(limit) { output_.clear(); }

  size_t write(uint8_t byte) override { return write(&byte, 1); }

  size_t write(const uint8_t* buffer, size_t size) override {
    if (!buffer || size == 0 || output_.size() > limit_ || size > limit_ - output_.size()) return 0;
    output_.append(reinterpret_cast<const char*>(buffer), size);
    return size;
  }

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

 private:
  std::string& output_;
  size_t limit_;
};

class FileWriteStream final : public Stream {
 public:
  FileWriteStream(FsFile& file, size_t total, HttpDownloader::ProgressCallback progress)
      : file_(file), total_(total), progress_(std::move(progress)) {}

  size_t write(uint8_t byte) override { return write(&byte, 1); }

  size_t write(const uint8_t* buffer, size_t size) override {
    // Legacy firmware download path; native App Store staging uses a pipe.
    const size_t written = file_.write(buffer, size);
    if (written != size) writeOk_ = false;
    downloaded_ += written;
    if (progress_ && total_ > 0) progress_(downloaded_, total_);
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

  if (const auto* api = invocationStreams(username, password)) {
    const auto receive = [](void* context, const uint8_t* bytes, uint32_t count) -> bool {
      return static_cast<Stream*>(context)->write(bytes, count) == count;
    };
    const auto result = RuntimeHttpStreams::fetch(api, url.c_str(), streamHooks(), receive, &outContent);
    if (result != RuntimeHttpStreams::Result::Ok) {
      LOG_ERR("HTTP", "Native metadata stream failed: %d", static_cast<int>(result));
      return false;
    }
    return true;
  }

  // Privileged provider task / non-ELF firmware path. The open_http adapter
  // uses this exact client, retaining the established redirect and TLS policy.
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
  // Release-level aggregate metadata is limited to 64 KiB and each manifest
  // is validated separately. Bound native string downloads *during* streaming;
  // the release asset listing instead uses its incremental CatalogReleaseStream.
  const bool native = invocationStreams(username, password) != nullptr;
  StringWriteStream stream(outContent, native ? kNativeMetadataLimit : std::numeric_limits<size_t>::max());
  return fetchUrl(url, stream, username, password);
}

HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& destPath,
                                                             ProgressCallback progress, const std::string& username,
                                                             const std::string& password) {
  if (!waitForNetworkReady()) {
    LOG_ERR("HTTP", "Network is not ready (no usable IP address)");
    return HTTP_ERROR;
  }

  // Every staged install, including the compatibility HTTPClient transport,
  // must refuse an existing .part. A read-only exists check is only an early
  // diagnostic; exclusive creation below owns the actual race-free operation.
  const bool staged = destPath.size() >= 6 && destPath.compare(destPath.size() - 5, 5, ".part") == 0;
  if (staged && (destPath.front() != '/' || destPath.compare(0, 4, "/sd/") == 0 ||
                 Storage.exists(destPath.c_str()))) {
    LOG_ERR("HTTP", "Staged destination exists or is invalid: %s", destPath.c_str());
    return FILE_ERROR;
  }

  // Native installers already create a transaction-specific, disposable .part
  // path. All of its bytes travel via HTTP stream -> lossless pipe -> exclusively
  // created file stream. The App Store still owns manifest policy and renames.
  const auto* streams = invocationStreams(username, password);
  if (streams && staged) {
    const size_t separator = destPath.find_last_of('/');
    if (separator == std::string::npos || separator == 0 ||
        !Storage.ensureDirectoryExists(destPath.substr(0, separator).c_str()) ||
        Storage.exists(destPath.c_str())) {
      LOG_ERR("HTTP", "Native staged destination is not new or not writable: %s", destPath.c_str());
      return FILE_ERROR;
    }
    const std::string streamPath = "/sd" + destPath;
    uint64_t transferred = 0;
    bool destinationCreated = false;
    int32_t httpOpenStatus = T5_STREAM_OK;
    auto reportProgress = [](void* context, uint64_t count) {
      auto* callback = static_cast<ProgressCallback*>(context);
      if (*callback) (*callback)(static_cast<size_t>(count), 0);
    };
    const auto result = RuntimeHttpStreams::download(streams, url.c_str(), streamPath.c_str(),
                                                      streamHooks(), reportProgress, &progress,
                                                      &transferred, &destinationCreated, &httpOpenStatus);
    if (result != RuntimeHttpStreams::Result::Ok) {
      if (httpOpenStatus == T5_STREAM_INVALID) {
        size_t callerUrlLength = 0;
        const auto callerUrlStatus = RuntimeHttpUrl::validate(url.c_str(), 1024, &callerUrlLength);
        LOG_ERR("HTTP", "Native staged URL at call site: reason=%s bytes=%u",
                RuntimeHttpUrl::statusName(callerUrlStatus),
                static_cast<unsigned>(callerUrlLength));
      }
      LOG_ERR("HTTP", "Native staged transfer failed: %d (open_http=%ld)",
              static_cast<int>(result), static_cast<long>(httpOpenStatus));
      // Exclusive open can fail because a different writer won the race after
      // exists(). That file is not ours, so never remove it on failed open.
      if (destinationCreated) Storage.remove(destPath.c_str());
      return result == RuntimeHttpStreams::Result::File ? FILE_ERROR : HTTP_ERROR;
    }
    // pipe DONE/finish prove the data-plane operation, not the SD artifact's
    // length. Reopen after file close and verify the byte count independently.
    HalFile file = Storage.open(destPath.c_str(), O_RDONLY);
    const bool complete = file.isOpen() && file.fileSize64() == transferred;
    if (file.isOpen()) file.close();
    if (!complete) {
      LOG_ERR("HTTP", "Native staged file differs from pipe transfer count");
      Storage.remove(destPath.c_str());
      return FILE_ERROR;
    }
    return OK;
  }

  // Compatibility path for non-native firmware downloads and destinations
  // that are not transactionally staged. No app-specific transport is added.
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

  // Non-transactional legacy destinations preserve overwrite semantics.
  // A .part destination must be created exclusively: the earlier exists
  // check cannot protect against a different writer racing this open.
  FsFile file;
  if (staged) {
    file = Storage.open(destPath.c_str(), O_WRONLY | O_CREAT | O_EXCL);
  } else {
    if (Storage.exists(destPath.c_str())) Storage.remove(destPath.c_str());
    (void)Storage.openFileForWrite("HTTP", destPath.c_str(), file);
  }
  if (!file.isOpen()) {
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

  if (contentLength > 0 && downloaded != contentLength) {
    LOG_ERR("HTTP", "Size mismatch: got %zu, expected %zu", downloaded, contentLength);
    Storage.remove(destPath.c_str());
    return HTTP_ERROR;
  }

  return OK;
}
