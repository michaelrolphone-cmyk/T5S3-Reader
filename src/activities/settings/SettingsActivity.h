#pragma once
#include <I18n.h>

#include <functional>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "activities/Activity.h"

enum class SettingType { TOGGLE, ENUM, ACTION, VALUE, STRING, TIMEZONE };

enum class SettingAction {
  None,
  RemapFrontButtons,
  CustomiseStatusBar,
  KOReaderSync,
  OPDSBrowser,
  Network,
  BatteryStatus,
  ClearCache,
  CheckForUpdates,
  SdFirmwareUpdate,
  Language,
  DownloadFonts,
};

struct SettingInfo {
  StrId nameId;
  SettingType type;
  uint8_t CrossPointSettings::* valuePtr = nullptr;
  std::vector<StrId> enumValues;
  std::vector<std::string> enumStringValues;
  SettingAction action = SettingAction::None;

  struct ValueRange {
    uint8_t min;
    uint8_t max;
    uint8_t step;
  };
  ValueRange valueRange = {};

  const char* key = nullptr;
  StrId category = StrId::STR_NONE_OPT;
  bool obfuscated = false;
  size_t stringOffset = 0;
  size_t stringMaxLen = 0;

  std::function<uint8_t()> valueGetter;
  std::function<void(uint8_t)> valueSetter;
  std::function<std::string()> stringGetter;
  std::function<void(const std::string&)> stringSetter;

  SettingInfo& withObfuscated() {
    obfuscated = true;
    return *this;
  }

  static SettingInfo Toggle(StrId nameId, uint8_t CrossPointSettings::* ptr, const char* key = nullptr,
                            StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::TOGGLE;
    s.valuePtr = ptr;
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo Enum(StrId nameId, uint8_t CrossPointSettings::* ptr, std::vector<StrId> values,
                          const char* key = nullptr, StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::ENUM;
    s.valuePtr = ptr;
    s.enumValues = std::move(values);
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo Action(StrId nameId, SettingAction action) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::ACTION;
    s.action = action;
    return s;
  }

  static SettingInfo Value(StrId nameId, uint8_t CrossPointSettings::* ptr, const ValueRange valueRange,
                           const char* key = nullptr, StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::VALUE;
    s.valuePtr = ptr;
    s.valueRange = valueRange;
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo String(StrId nameId, char* ptr, size_t maxLen, const char* key = nullptr,
                            StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::STRING;
    s.stringOffset = (size_t)ptr - (size_t)&SETTINGS;
    s.stringMaxLen = maxLen;
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo DynamicEnum(StrId nameId, std::vector<StrId> values, std::function<uint8_t()> getter,
                                 std::function<void(uint8_t)> setter, const char* key = nullptr,
                                 StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::ENUM;
    s.enumValues = std::move(values);
    s.valueGetter = std::move(getter);
    s.valueSetter = std::move(setter);
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo TimeZone(StrId nameId, char* ptr, size_t maxLen, const char* key = nullptr,
                              StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s = String(nameId, ptr, maxLen, key, category);
    s.type = SettingType::TIMEZONE;
    return s;
  }

  static SettingInfo DynamicString(StrId nameId, std::function<std::string()> getter,
                                   std::function<void(const std::string&)> setter, const char* key = nullptr,
                                   StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::STRING;
    s.stringGetter = std::move(getter);
    s.stringSetter = std::move(setter);
    s.key = key;
    s.category = category;
    return s;
  }
};

class SettingsActivity final : public Activity {
 public:
  explicit SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Settings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool onTouchTap(int16_t x, int16_t y) override;
  void render(RenderLock&&) override;

 private:
  bool launchAttempted = false;
  bool launchFailed = false;
};
