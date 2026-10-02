#!/usr/bin/env python3
"""Run the production ordinary-catalog refresh/selection path with host I/O.

The complete RuntimeOnlinePackages namespace is extracted unchanged from its
firmware header; independent parsing, aggregate parsing, merge and immutable
URL validation use the real portable headers. Only network, clock, Stream,
PSRAM and saved Wi-Fi platform dependencies are simulated. No device/network
or release-publication claim is made by this test.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "src/native/NativeOnlineOrdinaryCatalog.h"

HARNESS = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <vector>
#include "runtime/packages/PackageOnlineCatalog.h"

static uint32_t nowMs = 0;
static unsigned yields = 0, watchdogs = 0, psramAllocations = 0, failAllocation = 0;
static size_t livePsram = 0, peakPsram = 0;
static unsigned bulkAllocations = 0, failBulk = 0;
static std::map<void*, size_t> bulkObjects;
static constexpr uint32_t MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
static void* heap_caps_malloc(size_t bytes, uint32_t flags) {
  assert(flags == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (++bulkAllocations == failBulk) return nullptr;
  void* result = std::malloc(bytes);
  assert(result);
  bulkObjects[result] = bytes;
  return result;
}
static void heap_caps_free(void* value) {
  assert(bulkObjects.erase(value) == 1);
  std::free(value);
}
static uint32_t millis() { return nowMs; }
static void delay(uint32_t value) { nowMs += value; }
static void vTaskDelay(uint32_t value) { ++yields; nowMs += value; }
static void esp_task_wdt_reset() { ++watchdogs; }
static void logLine(const char*, const char*, ...) {}
#define LOG_INF logLine
#define LOG_ERR logLine
#define LOG_DBG logLine

class Stream {
 public:
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t*, size_t) = 0;
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  virtual void flush() = 0;
  virtual ~Stream() = default;
};
namespace RuntimeMemory {
class PsramBuffer {
 public:
  PsramBuffer(size_t size, bool allowInternal) {
    assert(!allowInternal);
    if (++psramAllocations == failAllocation) return;
    bytes_.resize(size);
    livePsram += size;
    peakPsram = std::max(peakPsram, livePsram);
  }
  ~PsramBuffer() { livePsram -= bytes_.size(); }
  explicit operator bool() const { return !bytes_.empty(); }
  uint8_t* data() { return bytes_.data(); }
  const char* chars() const { return reinterpret_cast<const char*>(bytes_.data()); }
 private:
  std::vector<uint8_t> bytes_;
};
}

struct WifiCredential { std::string ssid, password; };
struct FakeWifiStore {
  std::vector<WifiCredential> credentials;
  std::string last;
  unsigned loads = 0;
  void loadFromFile() { ++loads; }
  std::string getLastConnectedSsid() const { return last; }
  const WifiCredential* findCredential(const std::string& ssid) const {
    for (const auto& item : credentials) if (item.ssid == ssid) return &item;
    return nullptr;
  }
  const std::vector<WifiCredential>& getCredentials() const { return credentials; }
  void setLastConnectedSsid(const std::string& ssid) { last = ssid; }
};
static FakeWifiStore WIFI_STORE;
namespace RuntimeNetwork {
enum class ConnectionState { Connecting, Connected, Failed, NetworkNotFound };
struct State { ConnectionState connection; bool hasAddress; };
static bool available = true;
static State nextState{ConnectionState::Connected, true};
static std::string connectedSsid, connectedPassword;
static bool ready() { return available; }
static State state() { return nextState; }
struct Wifi {
  void connect(const char* ssid, const char* password) {
    connectedSsid = ssid;
    connectedPassword = password ? password : "";
  }
};
static Wifi& wifi() { static Wifi instance; return instance; }
}

struct Response {
  std::string body;
  bool fetched = true;
  size_t chunk = 127;
  uint32_t beforeMs = 0, chunkMs = 0, afterMs = 0;
  bool complete = true;
};
static std::map<std::string, Response> responses;
static std::vector<std::string> requests;
static void (*duringFetch)() = nullptr;
class HttpDownloader {
 public:
  static bool fetchUrl(const std::string& url, Stream& sink) {
    requests.push_back(url);
    if (duringFetch) duringFetch();
    const auto found = responses.find(url);
    assert(found != responses.end());
    const auto& response = found->second;
    nowMs += response.beforeMs;
    if (!response.fetched) return false;
    assert(response.chunk);
    for (size_t offset = 0; offset < response.body.size();) {
      const size_t size = std::min(response.chunk, response.body.size() - offset);
      nowMs += response.chunkMs;
      if (sink.write(reinterpret_cast<const uint8_t*>(response.body.data() + offset), size) != size)
        return false;
      offset += size;
    }
    nowMs += response.afterMs;
    return response.complete;
  }
};
'''

TESTS = r'''
namespace Catalog = RuntimeOnlinePackages::Catalog;
using RuntimePackages::CatalogPackage;
constexpr const char* kEmptyIndex = R"({"schema":1,"firmware":null,"apps":[],"drivers":[]})";

static std::string packageJson(const char* kind, const char* id, const char* version = "1.0.0") {
  return std::string("{\"kind\":\"") + kind + "\",\"id\":\"" + id +
      "\",\"version\":\"" + version + "\",\"artifact\":\"" + id +
      ".elf\",\"architecture\":\"xtensa-esp32s3\",\"archive\":\"" + kind + "-" + id + "-" +
      version + "-xtensa-esp32s3.rte.zip\",\"size_bytes\":100,\"sha256\":\"" +
      std::string(64, 'a') + "\"}";
}
static std::string aggregateJson(const char* tag = "v1.0.0", const char* version = "1.0.0") {
  return std::string("{\"schema\":1,\"release\":\"") + tag + "\",\"packages\":[" +
      packageJson("application", "reader", version) + "," +
      packageJson("driver", "sensor", version) + "," +
      packageJson("service", "archive", version) + "," +
      packageJson("provider", "codec", version) + "]}";
}
static std::string independentJson(const char* version = "2.0.0") {
  const std::string tag = std::string("driver-sensor-v") + version;
  const std::string asset = std::string("driver-sensor-") + version + "-xtensa-esp32s3.rte.zip";
  std::string entries;
  for (const char* name : {"sensor.elf", "provider-abi.v1", "privileged-imports.v1"}) {
    if (!entries.empty()) entries += ",";
    entries += std::string("{\"name\":\"") + name + "\",\"size_bytes\":96,\"sha256\":\"" +
        std::string(64, 'a') + "\",\"executable\":" +
        (!std::strcmp(name, "sensor.elf") ? "true" : "false") + "}";
  }
  return std::string("{\"schema\":1,\"firmware\":null,\"apps\":[],\"drivers\":[{") +
      "\"id\":\"sensor\",\"version\":\"" + version + "\",\"tag\":\"" + tag +
      "\",\"asset\":\"" + asset + "\",\"url\":\"https://github.com/" +
      "michaelrolphone-cmyk/T5S3-Reader/releases/download/" + tag + "/" + asset +
      "\",\"size\":2048,\"sha256\":\"" + std::string(64, 'a') +
      "\",\"format\":\"rte.zip\",\"architecture\":\"xtensa-esp32s3\",\"manifest\":{" +
      "\"schema\":1,\"kind\":\"driver\",\"id\":\"sensor\",\"version\":\"" + version +
      "\",\"architecture\":\"xtensa-esp32s3\",\"artifact\":\"sensor.elf\"," +
      "\"min_runtime_api\":2,\"entries\":[" + entries + "],\"requires\":[]}}]}";
}
static std::string independentAppJson() {
  std::string source = independentJson();
  auto replaceAll = [&](const std::string& from, const std::string& to) {
    size_t at = 0;
    while ((at = source.find(from, at)) != std::string::npos) {
      source.replace(at, from.size(), to); at += to.size();
    }
  };
  replaceAll("\"apps\":[],\"drivers\":[", "\"drivers\":[],\"apps\":[");
  replaceAll("driver-sensor-v", "app-reader-v");
  replaceAll("driver-sensor-", "application-reader-");
  replaceAll("sensor", "reader");
  replaceAll("\"kind\":\"driver\"", "\"kind\":\"application\"");
  replaceAll("provider-abi.v1", "reader.json");
  return source;
}
static void noPartialSelection() {
  assert(!Catalog::active());
  assert(Catalog::count(4) == 0);
  CatalogPackage item{};
  char tag[RuntimePackages::kOnlineReleaseTagBytes]{};
  assert(!Catalog::selected(4, 0, item, tag));
}
static void resetTransport() {
  Catalog::active().reset();
  assert(bulkObjects.empty());
  bulkAllocations = failBulk = 0;
  assert(livePsram == 0);
  nowMs = 0;
  yields = watchdogs = psramAllocations = failAllocation = 0;
  peakPsram = 0;
  responses.clear();
  requests.clear();
  responses[Catalog::kIndependentCatalog] = {kEmptyIndex};
  responses[Catalog::kLatestCatalog] = {aggregateJson()};
  RuntimeNetwork::available = true;
  RuntimeNetwork::nextState = {RuntimeNetwork::ConnectionState::Connected, true};
  RuntimeNetwork::connectedSsid.clear();
  RuntimeNetwork::connectedPassword.clear();
  WIFI_STORE = {};
  duringFetch = noPartialSelection;
}
static void assertFourKinds() {
  assert(Catalog::count(4) == 4);
  assert(Catalog::count(-1) == 0 && Catalog::count(5) == 0);
  for (int kind = 0; kind < 4; ++kind) {
    assert(Catalog::count(kind) == 1);
    CatalogPackage item{};
    char tag[RuntimePackages::kOnlineReleaseTagBytes]{};
    assert(Catalog::selected(kind, 0, item, tag));
    assert(static_cast<int>(item.identity.kind) == kind);
    assert(!std::strcmp(tag, "v1.0.0"));
    assert(!Catalog::selected(kind, 1, item, tag));
    assert(!Catalog::selected(-1, 0, item, tag));
    assert(!Catalog::selected(5, 0, item, tag));
  }
  assert((requests == std::vector<std::string>{Catalog::kIndependentCatalog, Catalog::kLatestCatalog}));
  assert(livePsram == 0);
  assert(peakPsram <= std::max(RuntimePackages::kIndependentCatalogMaxBytes,
                            RuntimePackages::kCatalogMaxBytes) + 1);
}
static void prime() {
  resetTransport();
  assert(Catalog::refresh());
  assertFourKinds();
  assert(yields && watchdogs >= yields);
  requests.clear();
}
static void assertRefreshFailure(size_t fetchCount) {
  assert(!Catalog::refresh());
  noPartialSelection();
  assert(requests.size() == fetchCount);
  assert(bulkObjects.empty());
  assert(livePsram == 0);
}
static void testSuccessAndPinnedSelection() {
  prime();
  CatalogPackage pinned{};
  char pinnedTag[RuntimePackages::kOnlineReleaseTagBytes]{};
  assert(Catalog::selected(1, 0, pinned, pinnedTag));
  std::string before;
  assert(RuntimePackages::onlineArchiveUrl(pinned, pinnedTag, "xtensa-esp32s3", before));
  assert(before.find("/download/v1.0.0/driver-sensor-1.0.0-") != std::string::npos);
  responses[Catalog::kLatestCatalog].body = aggregateJson("v2.0.0", "2.0.0");
  assert(Catalog::refresh());
  CatalogPackage current{};
  char currentTag[RuntimePackages::kOnlineReleaseTagBytes]{};
  assert(Catalog::selected(1, 0, current, currentTag));
  assert(!std::strcmp(current.identity.version, "2.0.0"));
  assert(!std::strcmp(currentTag, "v2.0.0"));
  assert(!std::strcmp(pinned.identity.version, "1.0.0"));
  assert(!std::strcmp(pinnedTag, "v1.0.0"));
  std::string after;
  assert(RuntimePackages::onlineArchiveUrl(pinned, pinnedTag, "xtensa-esp32s3", after));
  assert(before == after);
}
static void testIndependentApplicationSelection() {
  prime();
  responses[Catalog::kIndependentCatalog].body = independentAppJson();
  assert(Catalog::refresh() && Catalog::count(0) == 1 && Catalog::count(4) == 4);
  CatalogPackage app{}; char tag[RuntimePackages::kOnlineReleaseTagBytes]{};
  assert(Catalog::selected(0, 0, app, tag));
  assert(!std::strcmp(app.identity.version, "2.0.0") && !std::strcmp(tag, "app-reader-v2.0.0"));
  responses[Catalog::kLatestCatalog] = {"", false};
  assert(Catalog::refresh() && Catalog::count(0) == 1 && Catalog::count(4) == 1);
  responses[Catalog::kIndependentCatalog].body.pop_back();
  assert(!Catalog::refresh()); noPartialSelection();
}
static void testIndependentSelection() {
  prime();
  responses[Catalog::kIndependentCatalog].body = independentJson();
  assert(Catalog::refresh() && Catalog::count(4) == 4 && Catalog::count(1) == 1);
  CatalogPackage pinned{};
  char pinnedTag[RuntimePackages::kOnlineReleaseTagBytes]{};
  assert(Catalog::selected(1, 0, pinned, pinnedTag));
  assert(!std::strcmp(pinned.identity.version, "2.0.0"));
  assert(!std::strcmp(pinnedTag, "driver-sensor-v2.0.0"));
  std::string pinnedUrl;
  assert(RuntimePackages::onlineArchiveUrl(pinned, pinnedTag, "xtensa-esp32s3", pinnedUrl));
  assert(pinnedUrl.find("/download/driver-sensor-v2.0.0/driver-sensor-2.0.0-") != std::string::npos);

  // Removing the aggregate preserves current independent rows, never stale ones.
  responses[Catalog::kLatestCatalog].fetched = false;
  assert(Catalog::refresh() && Catalog::count(4) == 1 && Catalog::count(1) == 1);
  responses[Catalog::kIndependentCatalog].body = independentJson("3.0.0");
  assert(Catalog::refresh() && Catalog::count(4) == 1);
  CatalogPackage next{};
  char nextTag[RuntimePackages::kOnlineReleaseTagBytes]{};
  assert(Catalog::selected(1, 0, next, nextTag));
  assert(!std::strcmp(next.identity.version, "3.0.0"));
  assert(!std::strcmp(nextTag, "driver-sensor-v3.0.0"));
  std::string after;
  assert(RuntimePackages::onlineArchiveUrl(pinned, pinnedTag, "xtensa-esp32s3", after));
  assert(after == pinnedUrl);

  // Conflicting payloads for the same version fail during the real merge.
  responses[Catalog::kLatestCatalog] = {aggregateJson("v3.0.0", "3.0.0")};
  requests.clear();
  assertRefreshFailure(2);
  responses[Catalog::kIndependentCatalog].body = independentJson("4.0.0");
  assert(Catalog::refresh() && Catalog::count(4) == 4);
}
static void testInvalidSourcesAndRecovery() {
  for (const char* source : {Catalog::kIndependentCatalog, Catalog::kLatestCatalog}) {
    const size_t expectedFetches = source == Catalog::kIndependentCatalog ? 1 : 2;
    const std::string valid = source == Catalog::kIndependentCatalog ? kEmptyIndex : aggregateJson();
    const size_t budget = source == Catalog::kIndependentCatalog ?
        RuntimePackages::kIndependentCatalogMaxBytes : RuntimePackages::kCatalogMaxBytes;
    const std::vector<std::string> corrupt = {"", "{", "{}", valid.substr(0, valid.size() - 1),
        valid + "garbage", valid + std::string(budget, ' ')};
    for (const auto& body : corrupt) {
      prime();
      responses[source].body = body;
      assertRefreshFailure(expectedFetches);
      resetTransport();
      assert(Catalog::refresh());
      assertFourKinds();
    }
  }
  // A transport error after receiving bytes is corruption, not HTTP absence.
  for (const char* source : {Catalog::kIndependentCatalog, Catalog::kLatestCatalog}) {
    prime();
    responses[source].body.resize(17);
    responses[source].complete = false;
    assertRefreshFailure(source == Catalog::kIndependentCatalog ? 1 : 2);
  }
  prime();
  responses[Catalog::kIndependentCatalog].fetched = false;
  assertRefreshFailure(1);
  resetTransport();
  assert(Catalog::refresh());
  assertFourKinds();
  // HTTP absence of an optional aggregate is a valid fresh empty snapshot.
  prime();
  responses[Catalog::kLatestCatalog].fetched = false;
  assert(Catalog::refresh() && Catalog::active());
  assert(Catalog::count(4) == 0 && requests.size() == 2 && livePsram == 0);
}
static void testDeadlineAndAllocationFailure() {
  for (const char* source : {Catalog::kIndependentCatalog, Catalog::kLatestCatalog}) {
    prime();
    responses[source].beforeMs = Catalog::kRefreshTimeoutMs;
    assertRefreshFailure(source == Catalog::kIndependentCatalog ? 1 : 2);
  }
  for (unsigned allocation = 1; allocation <= 3; ++allocation) {
    prime();
    failBulk = bulkAllocations + allocation;
    assertRefreshFailure(allocation == 1 ? 0 : 2);
  }
  // Total budget is shared across requests, even without a single slow chunk.
  prime();
  responses[Catalog::kIndependentCatalog].afterMs = Catalog::kRefreshTimeoutMs / 2;
  responses[Catalog::kLatestCatalog].beforeMs = Catalog::kRefreshTimeoutMs / 2;
  assertRefreshFailure(2);
  for (unsigned allocation = 1; allocation <= 2; ++allocation) {
    prime();
    failAllocation = psramAllocations + allocation;
    assertRefreshFailure(allocation);
  }
  resetTransport();
  assert(Catalog::refresh());
  assertFourKinds();
}
static void testBoundedSink() {
  resetTransport();
  {
    Catalog::CatalogWork work;
    Catalog::BoundedCatalogSink sink(work, 3);
    assert(!sink.ready() && !sink.failed());
    assert(sink.write(static_cast<uint8_t>('a')) == 1);
    assert(sink.write(reinterpret_cast<const uint8_t*>("bc"), 2) == 2);
    assert(sink.size() == 3 && sink.ready() && !std::strcmp(sink.data(), "abc"));
    assert(sink.write(static_cast<uint8_t>('d')) == 0 && sink.failed());
    assert(sink.write(nullptr, 0) == 0 && !sink.ready());
    assert(sink.available() == 0 && sink.read() == -1 && sink.peek() == -1);
    sink.flush();
  }
  {
    Catalog::CatalogWork work;
    Catalog::BoundedCatalogSink sink(work, 3);
    assert(sink.write(nullptr, 1) == 0 && sink.failed());
  }
  {
    nowMs = UINT32_MAX - 10;
    Catalog::CatalogWork work;
    Catalog::BoundedCatalogSink sink(work, 3);
    nowMs += 20; // A wrap of millis alone is not a timeout.
    assert(sink.write(static_cast<uint8_t>('a')) == 1);
    nowMs += Catalog::kRefreshTimeoutMs;
    assert(sink.write(static_cast<uint8_t>('b')) == 0 && sink.failed());
  }
  assert(livePsram == 0);
}
static void testWifiFailureClearsSelection() {
  prime();
  RuntimeNetwork::available = false;
  assertRefreshFailure(0);
  assert(WIFI_STORE.loads == 1);
  prime();
  RuntimeNetwork::available = false;
  WIFI_STORE.credentials = {{"first", ""}, {"last", "secret"}};
  WIFI_STORE.last = "last";
  assert(Catalog::refresh());
  assert(RuntimeNetwork::connectedSsid == "last" && RuntimeNetwork::connectedPassword == "secret");
  assertFourKinds();
  prime();
  RuntimeNetwork::available = false;
  WIFI_STORE.credentials = {{"first", ""}};
  RuntimeNetwork::nextState = {RuntimeNetwork::ConnectionState::Connecting, false};
  assertRefreshFailure(0);
  assert(nowMs >= Catalog::kConnectTimeoutMs);
}
int main() {
  testSuccessAndPinnedSelection();
  testIndependentSelection();
  testIndependentApplicationSelection();
  testInvalidSourcesAndRecovery();
  testDeadlineAndAllocationFailure();
  testBoundedSink();
  testWifiFailureClearsSelection();
  Catalog::active().reset();
  assert(bulkObjects.empty());
  assert(livePsram == 0);
  std::puts("ordinary catalog refresh: four kinds, fail-closed sources, retry, pinned selection, bounds and Wi-Fi PASS");
}
'''


class OrdinaryCatalogRefreshTests(unittest.TestCase):
    def test_actual_refresh_and_selection(self):
        compiler = shutil.which(os.environ.get("CXX", "c++"))
        self.assertIsNotNone(compiler)
        source = HEADER.read_text()
        namespace = source[source.index("namespace RuntimeOnlinePackages {"):]
        with tempfile.TemporaryDirectory(prefix="ordinary-catalog-refresh-") as temp:
            source_path, binary = Path(temp) / "test.cpp", Path(temp) / "test"
            source_path.write_text(HARNESS + namespace + TESTS)
            command = [compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                       "-I" + str(ROOT / "src"), str(source_path), "-o", str(binary)]
            if os.environ.get("MV_SANITIZE") == "1":
                # Use -O1 for readable sanitizer diagnostics; keep -Werror.
                # GCC -O2 plus ASan warns in the existing manifest preflight.
                command[command.index("-O2")] = "-O1"
                command[2:2] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            subprocess.run(command, check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
