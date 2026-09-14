#include <T5AppApi.h>
#include <T5LanguageApi.h>

#include <CrossPointSettings.h>
#include <I18n.h>
#include <I18nKeys.h>

#include <algorithm>
#include <cstring>
#include <iterator>

namespace {

bool active() { return t5_app_get_api(T5_APP_ABI_VERSION) != nullptr; }

uint32_t countLanguages() {
  if (!active()) return 0;
  return static_cast<uint32_t>(std::size(SORTED_LANGUAGE_INDICES));
}

bool readLanguage(uint32_t displayIndex, t5_language_info_t* out) {
  if (!active() || !out || displayIndex >= std::size(SORTED_LANGUAGE_INDICES)) return false;
  std::memset(out, 0, sizeof(*out));
  const uint8_t languageId = SORTED_LANGUAGE_INDICES[displayIndex];
  out->language_id = languageId;
  out->selected = static_cast<uint8_t>(I18N.getLanguage()) == languageId ? 1 : 0;
  const char* name = I18N.getLanguageName(static_cast<Language>(languageId));
  if (!name) name = "";
  std::strncpy(out->name, name, sizeof(out->name) - 1);
  out->name[sizeof(out->name) - 1] = '\0';
  return true;
}

bool selectLanguage(uint8_t languageId) {
  if (!active()) return false;
  const auto* begin = std::begin(SORTED_LANGUAGE_INDICES);
  const auto* end = std::end(SORTED_LANGUAGE_INDICES);
  if (std::find(begin, end, languageId) == end) return false;

  I18N.setLanguage(static_cast<Language>(languageId));
  SETTINGS.language = languageId;
  SETTINGS.saveToFile();
  return true;
}

const t5_language_api_v1 api = {
    T5_LANGUAGE_API_VERSION,
    sizeof(t5_language_api_v1),
    countLanguages,
    readLanguage,
    selectLanguage,
};

}  // namespace

extern "C" const t5_language_api_v1* t5_language_get_api(uint32_t version) {
  return version == T5_LANGUAGE_API_VERSION && active() ? &api : nullptr;
}
