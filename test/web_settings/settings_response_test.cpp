#include <ArduinoJson.h>
#include <WebServer.h>
#include "network/SettingsJsonWriter.h"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
#define LOG_DBG(...) ((void)0)
uint32_t testMillis = 0;
unsigned testYields = 0;
struct CrossPointSettings { uint8_t fontFamily = 129; char text[128] = "Europe/Paris"; } SETTINGS;
enum class SettingType { TOGGLE, ENUM, VALUE, STRING, TIMEZONE, ACTION };
struct SettingInfo {
  const char* key = "fontFamily";
  int nameId = 0, category = 1;
  SettingType type = SettingType::ENUM;
  uint8_t CrossPointSettings::*valuePtr = &CrossPointSettings::fontFamily;
  std::function<uint8_t()> valueGetter;
  std::function<const char*()> stringGetter;
  std::vector<std::string> enumStringValues;
  std::vector<int> enumValues;
  struct { int min = 0, max = 10, step = 2; } valueRange;
  size_t stringMaxLen = 0, stringOffset = 0;
};
std::vector<SettingInfo> settings;
const auto& getSettingsList(const int*) { return settings; }
struct { int value = 0; const int& registry() { return value; } } sdFontSystem;
struct { const char* get(int id) { return id == 0 ? "Font Family" : "Reader"; } } I18N;
class CrossPointWebServer { public: WebServer* server; void handleGetSettings() const; };
struct TestAllocator : ArduinoJson::Allocator {
  bool fail = false;
  size_t live = 0;
  void* allocate(size_t size) override {
    if (fail) return nullptr;
    void* pointer = std::malloc(size);
    if (pointer) ++live;
    return pointer;
  }
  void deallocate(void* pointer) override {
    if (pointer) { --live; std::free(pointer); }
  }
  void* reallocate(void* pointer, size_t size) override {
    if (fail) return nullptr;
    if (!pointer) return allocate(size);
    return std::realloc(pointer, size);
  }
} allocator;
struct TestJsonDocument : ArduinoJson::JsonDocument {
  TestJsonDocument() : ArduinoJson::JsonDocument(&::allocator) {}
};
// Generated verbatim from the selected production revision by the runner.
#define JsonDocument TestJsonDocument
#include "handler.inc"
#undef JsonDocument
static void require(bool condition, const char* description) {
  if (!condition) { std::cerr << "FAIL: " << description << '\n'; std::exit(1); }
}
static JsonDocument response(WebServer& server) {
  CrossPointWebServer{&server}.handleGetSettings();
  require(allocator.live == 0, "per-request JSON allocation cleanup");
  require(server.status == 200 && server.alive && server.stops == 0, "successful response");
  require(server.terminators == 1, "one terminal chunk after JSON closes");
  require(server.largestChunk <= 512, "bounded HTTP chunk");
  JsonDocument parsed;
  require(!deserializeJson(parsed, server.body), "complete valid JSON");
  return parsed;
}
static void verifyEnum(const SettingInfo& setting) {
  settings = {setting}; WebServer server; auto result = response(server);
  require(result.size() == 1, "fontFamily must not disappear at the 512-byte boundary");
  require(result[0]["options"].size() == setting.enumStringValues.size(), "all choices retained");
  for (size_t i = 0; i < setting.enumStringValues.size(); ++i)
    require(result[0]["options"][i].as<std::string>() == setting.enumStringValues[i], "exact option order/bytes");
}
int main() {
  // Empty/action-only responses and every pre-existing setting kind.
  settings.clear(); WebServer empty; require(response(empty).size() == 0, "empty list");
  SettingInfo skip; skip.key = nullptr; SettingInfo action; action.type = SettingType::ACTION;
  settings = {skip, action}; WebServer skipped; require(response(skipped).size() == 0, "actions skipped");
  SettingInfo normal; normal.enumStringValues = {"Noto Serif", "Noto Sans"}; verifyEnum(normal);

  // Exact serialized object lengths straddling the old fixed-buffer limit.
  for (size_t length : {510u, 511u, 512u, 513u, 1024u, 4096u}) {
    JsonDocument expected;
    expected["key"] = "fontFamily"; expected["name"] = "Font Family";
    expected["category"] = "Reader"; expected["type"] = "enum"; expected["value"] = 129;
    expected["options"].to<JsonArray>().add("");
    const size_t overhead = measureJson(expected);
    SettingInfo boundary; boundary.enumStringValues = {std::string(length - overhead, 'x')};
    verifyEnum(boundary);
  }

  SettingInfo fonts;
  for (size_t i = 0; i < 130; ++i)
    fonts.enumStringValues.push_back("Font " + std::to_string(i) + " \"quoted\" \\ tab\t newline\n UTF-8 字");
  fonts.valuePtr = nullptr; fonts.valueGetter = [] { return uint8_t{129}; };
  verifyEnum(fonts);
  // Worst-case filesystem-length names, including maximum JSON escape expansion.
  SettingInfo maximum = fonts;
  for (auto& name : maximum.enumStringValues) name = std::string(255, '\x01');
  verifyEnum(maximum);

  SettingInfo toggle; toggle.key = "toggle"; toggle.type = SettingType::TOGGLE;
  SettingInfo value; value.key = "value"; value.type = SettingType::VALUE;
  SettingInfo text; text.key = "string"; text.type = SettingType::STRING;
  text.stringGetter = [] { return "escaped \"string\"\n字"; };
  SettingInfo zone; zone.key = "zone"; zone.type = SettingType::TIMEZONE;
  zone.stringMaxLen = sizeof(SETTINGS.text); zone.stringOffset = offsetof(CrossPointSettings, text);
  SettingInfo field = zone; field.key = "field"; field.type = SettingType::STRING;
  SettingInfo translated; translated.key = "builtin"; translated.enumValues = {0, 1};
  settings = {toggle, fonts, skip, value, action, text, zone, translated, field};
  WebServer mixed; auto result = response(mixed);
  require(result.size() == 7 && result[0]["type"] == "toggle", "mixed settings without stray commas");
  require(result[1]["value"] == 129 && result[1]["options"].size() == 130, "dynamic getter");
  require(result[2]["min"] == 0 && result[2]["max"] == 10 && result[2]["step"] == 2, "value range");
  require(result[3]["value"] == "escaped \"string\"\n字", "string getter escaping");
  require(result[4]["value"] == "Europe/Paris" && result[4]["type"] == "timezone", "timezone offset");
  require(result[5]["options"][0] == "Font Family" && result[6]["value"] == "Europe/Paris", "translated enum/string field");
  for (int repeat = 0; repeat < 20; ++repeat) {
    WebServer retry; response(retry); require(retry.body == mixed.body, "independent repeated requests");
  }

  // Partial network output is aborted, never presented as a successful short list.
  settings = {fonts}; WebServer broken; broken.failAfter = 2;
  CrossPointWebServer{&broken}.handleGetSettings();
  require(!broken.alive && broken.stops == 1 && broken.terminators == 0, "disconnect cleanup");
  broken = WebServer{}; response(broken);
  allocator.fail = true;
  WebServer exhausted; CrossPointWebServer{&exhausted}.handleGetSettings();
  require(exhausted.stops == 1 && exhausted.terminators == 0 && allocator.live == 0,
          "allocation failure aborts response and releases JSON state");
  allocator.fail = false;
  exhausted = WebServer{}; response(exhausted);
  WebServer expired; expired.sendDelay = 15000;
  CrossPointWebServer{&expired}.handleGetSettings();
  require(!expired.alive && expired.stops == 1 && expired.terminators == 0, "elapsed-time bound");
  WebServer lost; lost.alive = false; CrossPointWebServer{&lost}.handleGetSettings();
  require(lost.stops == 1 && lost.chunks.empty() && lost.terminators == 0, "initial disconnect");
  testMillis = UINT32_MAX - 3; WebServer rollover; rollover.sendDelay = 2; response(rollover);
  testMillis = UINT32_MAX - 3; WebServer lateRollover; lateRollover.sendDelay = 15000;
  CrossPointWebServer{&lateRollover}.handleGetSettings();
  require(lateRollover.stops == 1 && lateRollover.terminators == 0, "deadline across clock rollover");

  // Direct writer boundary and no accidental empty-buffer HTTP termination.
  testMillis = 0; WebServer writerServer; SettingsJsonWriter writer(writerServer);
  require(writer.flush() && writerServer.terminators == 0, "empty flush is not EOF");
  const std::string bytes(256 * 1024, 'x');
  require(writer.write(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()) == bytes.size(), "byte budget boundary");
  require(writer.flush() && writerServer.body == bytes, "exact chunk multiple");
  require(writer.write('x') == 0 && writerServer.stops == 1, "byte budget abort");
  require(writer.write('y') == 0 && !writer.flush() && writerServer.stops == 1, "failure stays sticky");
  require(testYields > 0, "real scheduler cooperation invoked");
  std::cout << "Web settings production-handler/ArduinoJson regression passed\n";
}
