namespace {
constexpr const char* jsonPath = "/.crosspoint/koreader.json";
constexpr const char* binaryPath = "/.crosspoint/koreader.bin";
constexpr const char* backupPath = "/.crosspoint/koreader.bin.bak";

void agree(bool binary) {
  t5_koreader_settings_t visible{};
  assert(readSettings(&visible));
  assert(visible.match_method == (binary ? T5_KOREADER_MATCH_BINARY : T5_KOREADER_MATCH_FILENAME));
  assert(selectedHash() == (binary ? "binary-id" : "filename-id"));
  assert(KOREADER_STORE.getMatchMethod() == (binary ? DocumentMatchMethod::BINARY : DocumentMatchMethod::FILENAME));
}

std::string jsonMode(const std::string& value) {
  return "{\"username\":\"fixture-user\",\"password_obf\":\"fixture-secret\","
         "\"serverUrl\":\"https://example.invalid\",\"matchMethod\":" + value + "}";
}

std::string legacy(unsigned value, bool hasMethod = true) {
  std::ostringstream bytes;
  serialization::writePod(bytes, uint8_t{1});
  serialization::writeString(bytes, "fixture-user");
  serialization::writeString(bytes, "");
  serialization::writeString(bytes, "https://example.invalid");
  if (hasMethod) serialization::writePod(bytes, static_cast<uint8_t>(value));
  return bytes.str();
}

void checkSaved(bool binary) {
  JsonDocument doc;
  assert(!deserializeJson(doc, Storage.files.at(jsonPath)));
  assert(doc["matchMethod"].is<uint8_t>());
  assert(doc["matchMethod"].as<uint8_t>() == (binary ? 1 : 0));
}
}

int main() {
  std::cout << "Testing actual ArduinoJson " << ARDUINOJSON_VERSION << std::endl;
  auto& store = KOReaderCredentialStore::getInstance();
  // A valid stored JSON document caused contradictory UI and sync behavior.
  bool resave = false;
  assert(JsonSettingsIO::loadKOReader(store, jsonMode("2").c_str(), &resave));
  agree(false);
  assert(resave);

  // Every representable persisted byte, plus repeated transitions through both
  // valid modes. Only exact Binary=1 may select binary hashing.
  for (unsigned value = 0; value < 256; ++value) {
    resave = false;
    assert(JsonSettingsIO::loadKOReader(store, jsonMode(std::to_string(value)).c_str(), &resave));
    agree(value == 1);
    assert(resave == (value > 1));
    assert(store.getUsername() == "fixture-user");
    assert(store.getPassword() == "fixture-secret");
    assert(store.getServerUrl() == "https://example.invalid");
    store.setMatchMethod(static_cast<DocumentMatchMethod>(value));
    agree(value == 1);
  }
  for (const char* invalid : {"-1", "256", "257", "65537", "1.5", "1.0", "true", "\"1\"", "[]", "{}"}) {
    resave = false;
    assert(JsonSettingsIO::loadKOReader(store, jsonMode(invalid).c_str(), &resave));
    agree(false);
    assert(resave);
  }
  for (const char* missing : {"{}", "{\"matchMethod\":null}"}) {
    resave = true;
    assert(JsonSettingsIO::loadKOReader(store, missing, &resave));
    agree(false);
    assert(!resave); // Missing historical fields already mean Filename.
  }
  assert(JsonSettingsIO::loadKOReader(store, jsonMode("255").c_str(), nullptr));
  agree(false);
  store.setMatchMethod(DocumentMatchMethod::BINARY);
  assert(!JsonSettingsIO::loadKOReader(store, "{broken", &resave));
  agree(true); // Failed parse never publishes a partial mode.
  assert(JsonSettingsIO::loadKOReader(store, jsonMode("0").c_str(), &resave));
  agree(false);

  // Public load path resaves normalized JSON. A failed best-effort resave cannot
  // make UI and sync diverge; the durable original remains available for retry.
  Storage.files.clear();
  Storage.files[jsonPath] = jsonMode("255");
  const auto original = Storage.files[jsonPath];
  Storage.failWrite = true;
  assert(store.loadFromFile());
  agree(false);
  assert(Storage.files[jsonPath] == original);
  Storage.failWrite = false;
  assert(store.loadFromFile());
  agree(false);
  checkSaved(false);
  const auto writes = Storage.writes;
  assert(store.loadFromFile());
  assert(Storage.writes == writes); // Normalized record is stable.

  // Real legacy deserialization and existing JSON publication/retirement path.
  for (unsigned value = 0; value < 256; ++value) {
    Storage.files.clear();
    Storage.files[binaryPath] = legacy(value);
    const auto sourceBytes = Storage.files[binaryPath];
    assert(store.loadFromFile());
    agree(value == 1);
    checkSaved(value == 1);
    assert(!Storage.exists(binaryPath));
    assert(Storage.files.at(backupPath) == sourceBytes);
  }
  Storage.files.clear();
  Storage.files[binaryPath] = legacy(1, false);
  assert(store.loadFromFile());
  agree(false);
  checkSaved(false);

  Storage.files.clear();
  Storage.files[binaryPath] = legacy(255);
  const auto originalLegacy = Storage.files[binaryPath];
  Storage.failWrite = true;
  assert(!store.loadFromFile());
  agree(false);
  assert(Storage.files[binaryPath] == originalLegacy && !Storage.exists(backupPath));
  Storage.failWrite = false;
  assert(store.loadFromFile());
  agree(false);
  checkSaved(false);
  assert(Storage.files.at(backupPath) == originalLegacy);
  // Open failure and a failed optional trailing mode read stay bounded. The
  // latter uses Filename rather than an uninitialized enum, as an omitted byte does.
  Storage.files.clear();
  Storage.files[binaryPath] = legacy(1);
  Storage.failOpen = true;
  const auto beforeOpenFailure = Storage.writes;
  assert(!store.loadFromFile());
  assert(Storage.writes == beforeOpenFailure && Storage.exists(binaryPath));
  Storage.failOpen = false;
  Storage.failLastByte = true;
  assert(store.loadFromFile());
  agree(false);
  checkSaved(false);
  Storage.failLastByte = false;
  Storage.files.clear();
  Storage.files[binaryPath] = legacy(1);
  assert(store.loadFromFile());
  agree(true);
  assert(fixtureFileOpens == fixtureFileCloses);
  std::cout << "KOReader match mode: real JSON/binary loaders, UI/hash dispatch, 256 values, failure/retry PASS\n";
}
