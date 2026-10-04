#include <HalStorage.h>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iosfwd>
#include <iostream>
#define private public
#include "CrossPointSettings.h"
#undef private
#include "I18nKeys.h"
#include "JsonSettingsIO.h"
namespace JsonSettingsIO {
bool saveSettings(const CrossPointSettings& settings, const char* path) {
  ++Storage.saves; Storage.events.push_back("save");
  if (Storage.saveFails) return false;
  Storage.files[path] = "{\"language\":" + std::to_string(settings.language) + "}";
  return true;
}
bool loadSettings(CrossPointSettings&, const char*, bool* needsResave) { if (needsResave) *needsResave = false; return true; }
}
constexpr char bin[] = "/.crosspoint/language.bin";
constexpr char bak[] = "/.crosspoint/language.bin.bak";
constexpr char json[] = "/.crosspoint/settings.json";
void reset(const std::string& bytes = std::string("\1\1", 2)) {
  assert(Storage.openHandles == 0); Storage = {};
  SETTINGS.language = 250;
  Storage.files[bin] = bytes;
  Storage.files[json] = "prior settings";
}
void check(bool condition, const char* message) {
  if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
void unchangedFailure(const char* message) {
  const auto oldFiles = Storage.files;
  check(!SETTINGS.migrateLanguageBinaryFile(), message);
  check(Storage.files == oldFiles, "failed read/validation changed persistent files");
  check(SETTINGS.language == 250, "failed read/validation changed live language");
  check(Storage.saves == 0 && Storage.renames == 0, "failed read/validation attempted publication");
  check(Storage.openHandles == 0, "failed read/validation leaked input handle");
}
int main() {
  reset(); Storage.openFails = true;
  unchangedFailure("open failure reported migration success");
  unchangedFailure("repeated open failure reported migration success");
  Storage.openFails = false;
  check(SETTINGS.migrateLanguageBinaryFile(), "open failure could not retry");

  for (const auto fault : {&HalStorage::readFails, &HalStorage::shortRead,
                           &HalStorage::readError, &HalStorage::closeFails}) {
    reset(); Storage.*fault = true;
    unchangedFailure("input fault reported migration success");
    Storage.*fault = false;
    check(SETTINGS.migrateLanguageBinaryFile(), "input fault could not retry");
  }
  for (const auto& bytes : {std::string(), std::string("\1", 1), std::string("\1\1\0", 3),
                            std::string("\0\1", 2), std::string("\2\1", 2),
                            std::string("\xff\1", 2), std::string("\1\xff", 2),
                            std::string({char(1), char(V1_LANGUAGE_COUNT)})}) {
    reset(bytes); unchangedFailure("invalid legacy record reported migration success");
    check(Storage.openHandles == 0, "invalid record leaked handle");
  }

  for (uint8_t index = 0; index < V1_LANGUAGE_COUNT; ++index) {
    const std::string bytes({char(1), char(index)});
    reset(bytes);
    check(SETTINGS.migrateLanguageBinaryFile(), "valid frozen language did not migrate");
    check(SETTINGS.language == static_cast<uint8_t>(V1_LANGUAGES[index]), "wrong frozen language mapping");
    check(!Storage.files.count(bin) && Storage.files[bak] == bytes, "successful migration did not preserve backup");
    check(Storage.files[json] == "{\"language\":" + std::to_string(SETTINGS.language) + "}", "destination has wrong language");
    check(Storage.events == std::vector<std::string>({"open", "read", "close", "save", "rename"}), "publication ordering is unsafe");
    check(Storage.openHandles == 0, "successful migration leaked input handle");
    check(!SETTINGS.migrateLanguageBinaryFile(), "already migrated input repeated success");
    check(Storage.saves == 1 && Storage.renames == 1, "already migrated input wrote again");
  }

  reset(); Storage.saveFails = true;
  const auto previous = Storage.files;
  for (int retry = 0; retry < 2; ++retry) {
    check(!SETTINGS.migrateLanguageBinaryFile(), "save failure reported success");
    check(Storage.files == previous && SETTINGS.language == 250, "save failure lost source or prior live value");
    check(Storage.renames == 0 && Storage.openHandles == 0, "save failure retired source or leaked handle");
  }
  Storage.saveFails = false;
  check(SETTINGS.migrateLanguageBinaryFile(), "save failure could not retry");

  reset(); Storage.renameFails = true;
  check(!SETTINGS.migrateLanguageBinaryFile(), "retirement failure reported success");
  check(Storage.files.count(bin) && !Storage.files.count(bak), "retirement failure lost source");
  check(SETTINGS.language == static_cast<uint8_t>(V1_LANGUAGES[1]), "committed language was incorrectly rolled back");
  check(Storage.files[json] == "{\"language\":" + std::to_string(SETTINGS.language) + "}", "retirement failure left RAM/disk disagreement");
  Storage.renameFails = false;
  check(SETTINGS.migrateLanguageBinaryFile(), "retirement failure could not retry");

  reset(); Storage.files[bak] = "unrelated prior backup";
  check(!SETTINGS.migrateLanguageBinaryFile(), "conflicting backup falsely retired source");
  check(Storage.files[bak] == "unrelated prior backup" && Storage.files.count(bin), "conflicting backup or source overwritten");

  reset(); Storage.files.erase(bin);
  check(!SETTINGS.migrateLanguageBinaryFile(), "missing source reported migration");
  check(Storage.saves == 0 && Storage.renames == 0 && Storage.events.empty(), "missing source performed I/O");

  // Exercise the existing boot callers, including a retry after a failed migration.
  reset(); Storage.files[json] = "{}"; Storage.saveFails = true;
  check(SETTINGS.loadFromFile(), "valid existing JSON should remain loadable");
  check(Storage.files.count(bin) && SETTINGS.language == 250, "JSON boot lost failed migration");
  Storage.saveFails = false;
  check(SETTINGS.loadFromFile() && !Storage.files.count(bin), "JSON boot did not retry migration");

  reset(); Storage.files.erase(json);
  check(SETTINGS.loadFromFile() && !Storage.files.count(bin), "language-only boot did not migrate");
  reset(); Storage.files.erase(json); Storage.saveFails = true;
  check(!SETTINGS.loadFromFile() && Storage.files.count(bin), "failed language-only boot lost source");
  check(SETTINGS.language == 250, "failed language-only boot retained uncommitted value");
  Storage.saveFails = false;
  check(SETTINGS.loadFromFile() && !Storage.files.count(bin), "language-only boot did not retry");

  reset(); Storage.files.erase(json);
  Storage.files["/.crosspoint/settings.bin"] = std::string("\1\1\0", 3);
  check(SETTINGS.loadFromFile() && !Storage.files.count(bin), "combined legacy boot did not migrate language");
  check(Storage.files.count("/.crosspoint/settings.bin.bak"), "combined legacy boot lost settings migration");
  check(Storage.openHandles == 0, "boot caller leaked input handle");
  std::cout << "Language migration: complete production settings, all frozen language mappings, fault/retry/cleanup and boot callers passed\n";
}
