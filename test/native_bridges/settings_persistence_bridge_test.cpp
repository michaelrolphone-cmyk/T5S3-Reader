#include <T5AppApi.h>
#include <T5LanguageApi.h>
#include <T5TimeZoneApi.h>

#include "CrossPointSettings.h"
#include "HalClock.h"
#include "I18n.h"
#include "TimeZoneCatalog.h"

#include <cassert>
#include <cstdint>
#include <cstring>

CrossPointSettings SETTINGS;
HalClock halClock;

extern "C" const t5_app_api_v1* t5_app_get_api(uint32_t version) {
  assert(version == T5_APP_ABI_VERSION);
  static const t5_app_api_v1 active_app{};
  return &active_app;
}

namespace TimeZoneCatalog {
static const TimeZoneEntry entries[] = {
    {"UTC", "UTC0", TimeZoneRegion::UTC},
    {"America/Los_Angeles", "PST8PDT", TimeZoneRegion::America},
    {"Europe/London", "GMT0BST", TimeZoneRegion::Europe},
};
const TimeZoneEntry* data() { return entries; }
uint16_t count() { return static_cast<uint16_t>(sizeof(entries) / sizeof(entries[0])); }
TimeZoneRegion regionOf(const char* id) {
  for (const auto& entry : entries) if (id && !std::strcmp(id, entry.id)) return entry.region;
  return TimeZoneRegion::UTC;
}
const char* regionDisplayName(TimeZoneRegion region) {
  switch (region) {
    case TimeZoneRegion::UTC: return "UTC";
    case TimeZoneRegion::America: return "America";
    case TimeZoneRegion::Europe: return "Europe";
    default: return "";
  }
}
void formatDisplayName(const char* id, char* buffer, size_t capacity) {
  copyId(buffer, capacity, id);
}
void copyId(char* destination, size_t capacity, const char* id) {
  if (!destination || !capacity) return;
  std::strncpy(destination, id ? id : "", capacity - 1);
  destination[capacity - 1] = '\0';
}
}

static void testLanguageSaveFailureRollsBack() {
  SETTINGS = {};
  SETTINGS.language = 0;
  SETTINGS.saveResult = false;
  I18N.setLanguage(Language::EN);
  const auto* api = t5_language_get_api(T5_LANGUAGE_API_VERSION);
  assert(api && api->select);
  assert(!api->select(static_cast<uint8_t>(Language::ES)));
  assert(SETTINGS.language == static_cast<uint8_t>(Language::EN));
  assert(I18N.getLanguage() == Language::EN);
  assert(SETTINGS.saveCalls == 1);
  SETTINGS.saveResult = true;
  assert(api->select(static_cast<uint8_t>(Language::ES)));
  assert(SETTINGS.language == static_cast<uint8_t>(Language::ES));
  assert(I18N.getLanguage() == Language::ES);
  assert(SETTINGS.saveCalls == 2);
}

static void testLanguageSaveSuccessCommits() {
  SETTINGS = {};
  SETTINGS.language = 0;
  SETTINGS.saveResult = true;
  I18N.setLanguage(Language::EN);
  const auto* api = t5_language_get_api(T5_LANGUAGE_API_VERSION);
  assert(api && api->select);
  assert(api->select(static_cast<uint8_t>(Language::ES)));
  assert(SETTINGS.language == static_cast<uint8_t>(Language::ES));
  assert(I18N.getLanguage() == Language::ES);
  assert(SETTINGS.saveCalls == 1);
}

static void set_timezone_fixture(bool save_result) {
  SETTINGS = {};
  TimeZoneCatalog::copyId(SETTINGS.timeZoneId, sizeof(SETTINGS.timeZoneId), "UTC");
  SETTINGS.rtcStoresUtc = 1;
  SETTINGS.rtcVariantHint = 2;
  SETTINGS.rtcReferenceEpoch = 123456;
  SETTINGS.saveResult = save_result;
  halClock.reset("UTC", true, 2, 123456);
}

static void testTimeZoneSaveFailureRollsBack() {
  set_timezone_fixture(false);
  halClock.reportedStoresUtc = false;
  const auto* api = t5_time_zone_get_api(T5_TIME_ZONE_API_VERSION);
  assert(api && api->select_city);
  assert(!api->select_city(static_cast<uint32_t>(TimeZoneRegion::America), 0));
  assert(!std::strcmp(SETTINGS.timeZoneId, "UTC"));
  assert(SETTINGS.rtcStoresUtc == 1);
  assert(SETTINGS.rtcVariantHint == 2);
  assert(SETTINGS.rtcReferenceEpoch == 123456);
  assert(!std::strcmp(halClock.zone_, "UTC"));
  assert(halClock.configuredStoresUtc);
  assert(halClock.configureCalls == 2);
  assert(halClock.syncCalls == 2);
  assert(SETTINGS.saveCalls == 1);
  SETTINGS.saveResult = true;
  assert(api->select_city(static_cast<uint32_t>(TimeZoneRegion::America), 0));
  assert(!std::strcmp(SETTINGS.timeZoneId, "America/Los_Angeles"));
  assert(SETTINGS.rtcStoresUtc == 0);
  assert(!std::strcmp(halClock.zone_, "America/Los_Angeles"));
  assert(halClock.configureCalls == 3);
  assert(halClock.syncCalls == 3);
  assert(SETTINGS.saveCalls == 2);
}

static void testTimeZoneSaveSuccessCommits() {
  set_timezone_fixture(true);
  halClock.reportedStoresUtc = false;
  const auto* api = t5_time_zone_get_api(T5_TIME_ZONE_API_VERSION);
  assert(api && api->select_city);
  assert(api->select_city(static_cast<uint32_t>(TimeZoneRegion::America), 0));
  assert(!std::strcmp(SETTINGS.timeZoneId, "America/Los_Angeles"));
  assert(SETTINGS.rtcStoresUtc == 0);
  assert(!std::strcmp(halClock.zone_, "America/Los_Angeles"));
  assert(halClock.configureCalls == 1);
  assert(halClock.syncCalls == 1);
  assert(SETTINGS.saveCalls == 1);
}

int main(int argc, char** argv) {
  if (argc == 2 && !std::strcmp(argv[1], "language")) {
    testLanguageSaveFailureRollsBack();
    testLanguageSaveSuccessCommits();
    return 0;
  }
  if (argc == 2 && !std::strcmp(argv[1], "timezone")) {
    testTimeZoneSaveFailureRollsBack();
    testTimeZoneSaveSuccessCommits();
    return 0;
  }
  testLanguageSaveFailureRollsBack();
  testLanguageSaveSuccessCommits();
  testTimeZoneSaveFailureRollsBack();
  testTimeZoneSaveSuccessCommits();
}
