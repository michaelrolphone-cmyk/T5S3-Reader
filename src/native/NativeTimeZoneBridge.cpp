#include <T5AppApi.h>
#include <T5TimeZoneApi.h>

#include <HalClock.h>
#include <TimeZoneCatalog.h>
#include <cstring>

#include "CrossPointSettings.h"

namespace {

bool active() { return t5_app_get_api(T5_APP_ABI_VERSION) != nullptr; }

void copyText(char* dst, size_t capacity, const char* src) {
  if (!dst || capacity == 0) return;
  if (!src) src = "";
  std::strncpy(dst, src, capacity - 1);
  dst[capacity - 1] = '\0';
}

uint32_t regionCount() {
  return active() ? static_cast<uint32_t>(TimeZoneRegion::Count) : 0u;
}

bool regionInfo(uint32_t region, t5_time_zone_region_info_t* out) {
  if (!active() || !out || region >= static_cast<uint32_t>(TimeZoneRegion::Count)) return false;
  *out = {};
  const auto value = static_cast<TimeZoneRegion>(region);
  copyText(out->name, sizeof(out->name), TimeZoneCatalog::regionDisplayName(value));
  out->selected = TimeZoneCatalog::regionOf(SETTINGS.timeZoneId) == value ? 1u : 0u;
  return true;
}

uint32_t cityCount(uint32_t region) {
  if (!active() || region >= static_cast<uint32_t>(TimeZoneRegion::Count)) return 0;
  const auto value = static_cast<TimeZoneRegion>(region);
  uint32_t count = 0;
  const auto* entries = TimeZoneCatalog::data();
  const uint16_t n = TimeZoneCatalog::count();
  for (uint16_t i = 0; i < n; ++i) if (entries[i].region == value) ++count;
  return count;
}

bool cityInfo(uint32_t region, uint32_t city, t5_time_zone_city_info_t* out) {
  if (!active() || !out || region >= static_cast<uint32_t>(TimeZoneRegion::Count)) return false;
  const auto value = static_cast<TimeZoneRegion>(region);
  const auto* entries = TimeZoneCatalog::data();
  const uint16_t n = TimeZoneCatalog::count();
  uint32_t current = 0;
  for (uint16_t i = 0; i < n; ++i) {
    if (entries[i].region != value) continue;
    if (current++ != city) continue;
    *out = {};
    copyText(out->id, sizeof(out->id), entries[i].id);
    TimeZoneCatalog::formatDisplayName(entries[i].id, out->name, sizeof(out->name));
    out->selected = std::strcmp(entries[i].id, SETTINGS.timeZoneId) == 0 ? 1u : 0u;
    return true;
  }
  return false;
}

bool selectCity(uint32_t region, uint32_t city) {
  t5_time_zone_city_info_t info{};
  if (!cityInfo(region, city, &info)) return false;
  TimeZoneCatalog::copyId(SETTINGS.timeZoneId, sizeof(SETTINGS.timeZoneId), info.id);
  halClock.configure(SETTINGS.timeZoneId, SETTINGS.rtcStoresUtc != 0, SETTINGS.rtcVariantHint,
                     SETTINGS.rtcReferenceEpoch);
  (void)halClock.syncSystemTimeFromRtc();
  SETTINGS.rtcStoresUtc = halClock.getRtcStoresUtc() ? 1 : 0;
  return SETTINGS.saveToFile();
}

const t5_time_zone_api_v1 api = {
    T5_TIME_ZONE_API_VERSION,
    sizeof(t5_time_zone_api_v1),
    regionCount,
    regionInfo,
    cityCount,
    cityInfo,
    selectCity,
};

}  // namespace

extern "C" const t5_time_zone_api_v1* t5_time_zone_get_api(uint32_t version) {
  return version == T5_TIME_ZONE_API_VERSION && active() ? &api : nullptr;
}
