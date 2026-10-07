static CrossPointState seeded() {
  CrossPointState s;
  s.openEpubPath = "/keep.epub";
  s.pushRecentSleep(1234);
  s.readerActivityLoadCount = 2;
  s.lastSleepFromReader = true;
  return s;
}
static void equal(const CrossPointState& a, const CrossPointState& b) {
  assert(a.openEpubPath == b.openEpubPath);
  assert(a.recentSleepPos == b.recentSleepPos && a.recentSleepFill == b.recentSleepFill);
  assert(a.readerActivityLoadCount == b.readerActivityLoadCount);
  assert(a.lastSleepFromReader == b.lastSleepFromReader);
  for (unsigned i = 0; i < CrossPointState::SLEEP_RECENT_COUNT; ++i)
    assert(a.recentSleepImages[i] == b.recentSleepImages[i]);
}
static std::string legacy() {
  std::string s;
  auto pod = [&s](const auto& value) { s.append(reinterpret_cast<const char*>(&value), sizeof(value)); };
  pod(uint8_t{4});
  const std::string path = "/legacy.epub";
  pod(static_cast<uint32_t>(path.size())); s += path;
  pod(uint8_t{9}); pod(uint8_t{3}); pod(true);
  return s;
}
int main() {
  unsigned rejected = 0;
  for (const char* json : {"{}", "[]", "null", "42", "true", "\"text\"", "{", "",
       R"({"openEpubPath":null})", R"({"openEpubPath":42})", R"({"other":"x"})",
       R"({"openEpubPath":"x","readerActivityLoadCount":256})",
       R"({"openEpubPath":"x","readerActivityLoadCount":-1})",
       R"({"openEpubPath":"x","readerActivityLoadCount":1.5})",
       R"({"openEpubPath":"x","readerActivityLoadCount":"1"})",
       R"({"openEpubPath":"x","lastSleepFromReader":1})",
       R"({"openEpubPath":"x","lastSleepFromReader":null})",
       R"({"openEpubPath":"x","lastSleepImage":256})",
       R"({"openEpubPath":"x","recentSleepPos":0})",
       R"({"openEpubPath":"x","recentSleepImages":[],"recentSleepPos":0})",
       R"({"openEpubPath":"x","recentSleepImages":{},"recentSleepPos":0,"recentSleepFill":0})",
       R"({"openEpubPath":"x","recentSleepImages":[],"recentSleepPos":16,"recentSleepFill":0})",
       R"({"openEpubPath":"x","recentSleepImages":[],"recentSleepPos":0,"recentSleepFill":1})",
       R"({"openEpubPath":"x","recentSleepImages":[1],"recentSleepPos":0,"recentSleepFill":1})",
       R"({"openEpubPath":"x","recentSleepImages":[65536],"recentSleepPos":1,"recentSleepFill":1})",
       R"({"openEpubPath":"x","recentSleepImages":[-1],"recentSleepPos":1,"recentSleepFill":1})",
       R"({"openEpubPath":"x","recentSleepImages":[null],"recentSleepPos":1,"recentSleepFill":1})",
       R"({"openEpubPath":"x","recentSleepImages":[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],"recentSleepPos":0,"recentSleepFill":0})"}) {
    auto s = seeded(); const auto before = s;
    assert(!JsonSettingsIO::loadState(s, json)); equal(s, before);
    Storage = {}; Storage.files["/.crosspoint/state.json"] = json;
    assert(!s.loadFromFile()); equal(s, before); assert(Storage.writes == 0 && Storage.renames == 0);
    ++rejected;
  }
  // Native writer roundtrip including a full, wrapped ring and real zero values.
  Storage = {}; auto original = seeded();
  for (unsigned i = 0; i < 40; ++i) original.pushRecentSleep(i);
  assert(original.saveToFile()); CrossPointState loaded; assert(loaded.loadFromFile()); equal(original, loaded);
  auto json = Storage.files["/.crosspoint/state.json"];
  for (unsigned i = 0; i < 3; ++i) { loaded = seeded(); assert(loaded.loadFromFile()); equal(original, loaded); }
  // Minimal historical JSON and the pre-ring single-image format remain valid.
  for (const char* j : {R"({"openEpubPath":""})", R"({"openEpubPath":"/book.epub","unknown":42})"}) {
    auto s = seeded(); assert(JsonSettingsIO::loadState(s,j));
    assert(s.recentSleepFill == 0 && s.recentSleepPos == 0 && s.readerActivityLoadCount == 0 && !s.lastSleepFromReader);
  }
  auto s = seeded();
  assert(JsonSettingsIO::loadState(s,R"({"openEpubPath":"/old.epub","lastSleepImage":9,"readerActivityLoadCount":255,"lastSleepFromReader":true})"));
  assert(s.isRecentSleep(9,1) && s.readerActivityLoadCount == 255 && s.lastSleepFromReader);
  assert(JsonSettingsIO::loadState(s,R"({"openEpubPath":"","lastSleepImage":255})")); assert(s.recentSleepFill == 0);
  assert(JsonSettingsIO::loadState(s,R"({"openEpubPath":"","recentSleepImages":[0,65535],"recentSleepPos":2,"recentSleepFill":2})"));
  assert(s.isRecentSleep(65535,1));
  // Invalid JSON falls back to the real binary decoder, then saves and renames.
  for (const char* bad : {"{}", "[]", "{", ""}) {
    Storage = {}; Storage.files["/.crosspoint/state.json"] = bad;
    Storage.files["/.crosspoint/state.bin"] = legacy(); s = {};
    assert(s.loadFromFile()); assert(s.openEpubPath == "/legacy.epub" && s.isRecentSleep(9,1));
    assert(s.readerActivityLoadCount == 3 && s.lastSleepFromReader);
    assert(Storage.writes == 1 && Storage.renames == 1);
    assert(Storage.exists("/.crosspoint/state.bin.bak") && !Storage.exists("/.crosspoint/state.bin"));
    auto recovered = s; s = {}; assert(s.loadFromFile()); equal(s,recovered);
  }
  // A failed recovery save leaves the legacy source for a subsequent retry.
  Storage = {}; Storage.files["/.crosspoint/state.json"] = "{}";
  Storage.files["/.crosspoint/state.bin"] = legacy(); Storage.failWrite = true; s = seeded();
  const auto beforeFailedSave = s;
  assert(!s.loadFromFile()); equal(s, beforeFailedSave); assert(Storage.renames == 0 && Storage.exists("/.crosspoint/state.bin"));
  assert(Storage.files["/.crosspoint/state.json"] == "{}");
  Storage.failWrite = false; assert(s.loadFromFile()); assert(Storage.renames == 1);
  // Valid JSON wins over a legacy source and never triggers migration writes.
  Storage = {}; Storage.files["/.crosspoint/state.json"] = json; Storage.files["/.crosspoint/state.bin"] = legacy();
  s = {}; assert(s.loadFromFile()); equal(s, original); assert(Storage.writes == 0 && Storage.renames == 0);
  Storage = {}; Storage.files["/.crosspoint/state.json"] = "{}"; Storage.files["/.crosspoint/state.bin"] = legacy();
  Storage.failOpen = true; s = seeded(); const auto before = s;
  assert(!s.loadFromFile()); equal(s, before); assert(Storage.writes == 0);
  assert(fixtureFileOpens == fixtureFileCloses);
  std::cout << rejected << " invalid schemas rejected atomically; roundtrip, legacy recovery, failure/retry and cleanup passed\n";
}
