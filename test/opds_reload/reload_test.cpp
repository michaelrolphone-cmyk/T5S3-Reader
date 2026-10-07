#include <ArduinoJson.h>
#include <HalStorage.h>
#include <T5AppApi.h>
#include <T5OpdsApi.h>

#include <cassert>
#include <cstring>
#include <iostream>

#include "CrossPointSettings.h"
#include "JsonSettingsIO.h"
#include "Logging.h"
#include "OpdsServerStore.h"

StorageFixture Storage;
SettingsFixture SETTINGS;
static bool appActive = true;
static const char* const PATH = "/.crosspoint/opds.json";
static const char* const VALID = R"({"servers":[{"name":"Books A","url":"https://a.example"},{"name":"Books B","url":"https://b.example"}]})";

extern "C" const t5_app_api_v1* t5_app_get_api(uint32_t version) {
  return appActive && version == T5_APP_ABI_VERSION ? reinterpret_cast<const t5_app_api_v1*>(1) : nullptr;
}

// Credential encoding and persistence endpoints are fixtures. JSON parsing and
// every store/bridge operation under test are unchanged production source.
namespace obfuscation {
std::string deobfuscateFromBase64(const char* value, bool* ok) {
  *ok = *value != '\0';
  return value;
}
}
#include "load_opds.inc"

bool JsonSettingsIO::saveOpds(const OpdsServerStore& store, const char* path) {
  JsonDocument document;
  JsonArray list = document["servers"].to<JsonArray>();
  for (const auto& server : store.getServers()) {
    JsonObject row = list.add<JsonObject>();
    row["name"] = server.name;
    row["url"] = server.url;
    row["username"] = server.username;
    row["password_obf"] = server.password;
  }
  std::string bytes;
  serializeJson(document, bytes);
  return Storage.writeFile(path, bytes);
}

static void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

static void freshValid() {
  Storage = {};
  SETTINGS = {};
  appActive = true;
  Storage.files[PATH] = VALID;
  expect(OPDS_STORE.loadFromFile() && OPDS_STORE.getCount() == 2, "valid reload replaces complete snapshot");
}

static void expectEmpty(const t5_opds_api_v1* api) {
  expect(OPDS_STORE.getCount() == 0 && OPDS_STORE.getServer(0) == nullptr, "failed reload invalidates stale store");
  expect(api->count() == 0, "native count hides failed reload");
  t5_opds_server_t output;
  std::memset(&output, 0x5a, sizeof(output));
  expect(!api->read(0, &output), "native read rejects failed reload");
  const t5_opds_server_t empty{};
  expect(std::memcmp(&output, &empty, sizeof(output)) == 0, "failed read clears prior output");
  expect(Storage.writes == 0 && SETTINGS.saves == 0, "load failure has no persistence side effects");
}

int main() {
  const t5_opds_api_v1* api = t5_opds_get_api(T5_OPDS_API_VERSION);
  expect(api != nullptr && t5_opds_get_api(999) == nullptr, "existing API version admission");
  freshValid();
  t5_opds_server_t output{};
  expect(api->count() == 2 && api->read(1, &output), "valid native snapshot");
  expect(std::strcmp(output.name, "Books B") == 0 && std::strcmp(output.url, "https://b.example") == 0,
         "normal fields and order preserved");
  const int reads = Storage.reads;
  expect(!api->read(0, nullptr) && Storage.reads == reads, "null output does not load");
  expect(!api->read(2, &output), "out-of-range read rejected");
  appActive = false;
  const int inactiveReads = Storage.reads;
  expect(t5_opds_get_api(T5_OPDS_API_VERSION) == nullptr && api->count() == 0 && !api->read(0, &output),
         "inactive context rejected");
  expect(Storage.reads == inactiveReads, "inactive context performs no I/O");

  for (int fault = 0; fault < 4; ++fault) {
    freshValid();
    if (fault == 0) Storage.files.clear();
    if (fault == 1) Storage.files[PATH].clear();
    if (fault == 2) Storage.files[PATH] = "{\"servers\":[{\"name\":\"partial\"},";
    if (fault == 3) Storage.failRead = true;
    expect(!OPDS_STORE.loadFromFile(), "missing/empty/malformed/read-failed reload reports failure");
    expectEmpty(api);
    expect(!OPDS_STORE.loadFromFile(), "repeated failure remains failure");
    expectEmpty(api);
    Storage.failRead = false;
    Storage.files[PATH] = VALID;
    expect(api->count() == 2 && api->read(1, &output), "successful retry reconstructs snapshot");
    expect(std::strcmp(output.name, "Books B") == 0, "retry returns current data");
  }

  freshValid();
  Storage.files[PATH] = R"({"servers":[]})";
  expect(OPDS_STORE.loadFromFile(), "valid empty list remains a successful load");
  expectEmpty(api);
  freshValid();
  Storage.files[PATH] = R"({"servers":[{"name":"New","url":"https://new.example"}]})";
  expect(api->count() == 1 && api->read(0, &output) && std::strcmp(output.name, "New") == 0,
         "replacement does not retain removed rows");

  // Existing legacy migration is retained, but must not append to an old load.
  freshValid();
  Storage.files.clear();
  std::strcpy(SETTINGS.opdsServerUrl, "https://legacy.example");
  Storage.failWrite = true;
  expect(!OPDS_STORE.loadFromFile() && OPDS_STORE.getCount() == 0, "failed legacy migration remains empty");
  expect(SETTINGS.opdsServerUrl[0] != '\0' && SETTINGS.saves == 0, "failed migration retains source");
  Storage.failWrite = false;
  expect(OPDS_STORE.loadFromFile() && OPDS_STORE.getCount() == 1, "migration retry creates one current record");
  expect(SETTINGS.opdsServerUrl[0] == '\0' && SETTINGS.saves == 1, "successful migration retires legacy fields");
  expect(api->read(0, &output) && std::strcmp(output.url, "https://legacy.example") == 0,
         "persisted migration reloads normally");
  expect(api->count() == 1, "repeated migrated reload does not duplicate");
  std::cout << "PASS: OPDS production store/JSON loader/bridge normal, failure, retry and cleanup\n";
}
