#include <ArduinoJson.h>
#include "OpdsServerStore.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#undef OPDS_STORE
#define LOG_DBG(...) ((void)0)
constexpr int CONTENT_LENGTH_UNKNOWN = -1;
struct StoreFixture {
  std::vector<OpdsServer> records;
  const auto& getServers() const { return records; }
} OPDS_STORE;
struct HttpFixture {
  std::string body, contentType;
  int status = 0, contentLength = 0, sends = 0, terminators = 0;
  size_t largestChunk = 0;
  void setContentLength(int value) { contentLength = value; }
  void send(int code, const char* type, const char* initial) {
    status = code; contentType = type; body = initial; ++sends;
  }
  void sendContent(const char* value) {
    const std::string chunk(value);
    if (chunk.empty()) ++terminators;
    largestChunk = std::max(largestChunk, chunk.size());
    body += chunk;
  }
};
struct CrossPointWebServer {
  HttpFixture* server;
  void handleGetOpdsServers() const;
};
// This is the whole unmodified method extracted from the selected production source.
#include "handler.inc"

static void require(bool condition, const char* message) {
  if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
static JsonDocument expectedRecord(const OpdsServer& item, size_t index) {
  JsonDocument object;
  object["index"] = index;
  object["name"] = item.name;
  object["url"] = item.url;
  object["username"] = item.username;
  object["hasPassword"] = !item.password.empty();
  return object;
}
static unsigned requests = 0;
static void verify(const std::vector<OpdsServer>& records, HttpFixture& http) {
  OPDS_STORE.records = records;
  http = {};
  CrossPointWebServer{&http}.handleGetOpdsServers();
  ++requests;
  require(http.status == 200 && http.sends == 1 && http.contentType == "application/json",
          "one JSON success response");
  require(http.contentLength == CONTENT_LENGTH_UNKNOWN && http.terminators == 1,
          "existing streaming response closes exactly once");
  require(http.largestChunk < 512, "existing output chunk bound retained");
  JsonDocument parsed;
  const auto error = deserializeJson(parsed, http.body);
  if (error) std::cerr << "Response: " << http.body << '\n';
  require(!error && parsed.is<JsonArray>(), "complete parseable JSON array after skipped records");
  JsonDocument expected;
  auto array = expected.to<JsonArray>();
  for (size_t i = 0; i < records.size(); ++i) {
    auto object = expectedRecord(records[i], i);
    // This repair preserves the existing oversized-record omission policy.
    if (measureJson(object) < 512) array.add(object.as<JsonObject>());
  }
  require(parsed == expected, "exact surviving fields, source indices, order and password flags");
  require(http.body.find("do-not-expose-secret") == std::string::npos, "password values concealed");
  require(OPDS_STORE.records.size() == records.size(), "read-only source count");
  for (size_t i = 0; i < records.size(); ++i)
    require(OPDS_STORE.records[i].name == records[i].name && OPDS_STORE.records[i].url == records[i].url &&
                OPDS_STORE.records[i].username == records[i].username &&
                OPDS_STORE.records[i].password == records[i].password, "read-only source contents");
}
int main() {
  HttpFixture http;
  const OpdsServer normal{"Library", "https://example.test/catalog", "reader", "do-not-expose-secret"};
  OpdsServer large = normal;
  large.name = std::string(600, 'A');
  // Original baseline fails here: its response starts with "[," .
  verify({large, normal}, http);
  verify({}, http);
  verify({normal}, http);
  verify({large}, http);

  // Every skip/emission position in the production store's eight-record capacity.
  for (unsigned mask = 0; mask < 256; ++mask) {
    std::vector<OpdsServer> records;
    for (unsigned i = 0; i < 8; ++i) {
      auto item = (mask & (1u << i)) ? large : normal;
      item.url += "/" + std::to_string(i);
      if (i % 2) item.password.clear();
      records.push_back(item);
    }
    verify(records, http);
  }
  // Exact serialized lengths, including the null-terminator boundary.
  for (size_t length : {510u, 511u, 512u, 513u, 1024u}) {
    auto item = normal;
    item.name.clear();
    const auto overhead = measureJson(expectedRecord(item, 0));
    item.name.assign(length - overhead, 'x');
    require(measureJson(expectedRecord(item, 0)) == length, "exact serialization boundary fixture");
    verify({item, normal}, http);
  }
  // Escaping can exceed the limit even with fewer than 512 input bytes.
  auto escaped = normal;
  escaped.name = std::string(250, '"');
  require(escaped.name.size() < 512 && measureJson(expectedRecord(escaped, 0)) >= 512,
          "oversized escaped object fixture");
  verify({escaped, normal}, http);
  auto unicode = normal;
  unicode.name = "Librairie \u00e9 \u8aad\u66f8 \ud83d\udcda \"quoted\"";
  unicode.url += "?q=\"a\"&path=\\";
  unicode.username = "reader\nnext\tline";
  verify({large, unicode, large, normal}, http);

  // Consecutive requests recover after omitted-only data, then reset to empty/normal data.
  for (int repeat = 0; repeat < 5; ++repeat) {
    verify({large, large}, http);
    verify({large, normal}, http);
    verify({normal, normal}, http);
    verify({}, http);
  }
  std::cout << "OPDS response regression PASS: " << requests << " requests\n";
}
