#include "NativeSettingsBridge.h"

#include <Board.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <TimeZoneCatalog.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "NativeAppHost.h"
#include "SdCardFontGlobals.h"
#include "SettingsList.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/settings/BatteryStatusActivity.h"
#include "activities/settings/ButtonRemapActivity.h"
#include "activities/settings/ClearCacheActivity.h"
#include "activities/settings/FontDownloadActivity.h"
#include "activities/settings/FontSelectionActivity.h"
#include "activities/settings/KOReaderSettingsActivity.h"
#include "activities/settings/LanguageSelectActivity.h"
#include "activities/settings/OpdsServerListActivity.h"
#include "activities/settings/OtaUpdateActivity.h"
#include "activities/settings/SdFirmwareUpdateActivity.h"
#include "activities/settings/StatusBarSettingsActivity.h"
#include "activities/settings/TimeZoneSelectActivity.h"
#include "components/UITheme.h"

namespace {
constexpr uint32_t kCategoryCount = 4;
const std::array<StrId, kCategoryCount> kCategoryNames = {
    StrId::STR_CAT_DISPLAY, StrId::STR_CAT_READER, StrId::STR_CAT_CONTROLS, StrId::STR_CAT_SYSTEM};

enum class PendingAction : uint8_t {
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
  FontSelection,
  TimeZone,
};

struct BridgeState {
  GfxRenderer* renderer = nullptr;
  MappedInputManager* input = nullptr;
  std::array<std::vector<SettingInfo>, kCategoryCount> settings;
  bool built = false;
  PendingAction requestedAction = PendingAction::None;
};

BridgeState state;

void copyText(char* dst, size_t capacity, const char* src) {
  if (!dst || capacity == 0) return;
  if (!src) src = "";
  std::strncpy(dst, src, capacity - 1);
  dst[capacity - 1] = '\0';
}

int categoryIndex(StrId category) {
  for (uint32_t i = 0; i < kCategoryCount; ++i) {
    if (kCategoryNames[i] == category) return static_cast<int>(i);
  }
  return -1;
}

void appendDeviceActions() {
  auto& reader = state.settings[1];
  auto& controls = state.settings[2];
  auto& system = state.settings[3];

  controls.insert(controls.begin(),
                  SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS, SettingAction::RemapFrontButtons));
  system.push_back(SettingInfo::Action(StrId::STR_WIFI_NETWORKS, SettingAction::Network));
  system.push_back(SettingInfo::Action(StrId::STR_BATTERY_STATUS, SettingAction::BatteryStatus));
  system.push_back(SettingInfo::Action(StrId::STR_KOREADER_SYNC, SettingAction::KOReaderSync));
  system.push_back(SettingInfo::Action(StrId::STR_OPDS_SERVERS, SettingAction::OPDSBrowser));
  system.push_back(SettingInfo::Action(StrId::STR_CLEAR_READING_CACHE, SettingAction::ClearCache));
  system.push_back(SettingInfo::Action(StrId::STR_CHECK_UPDATES, SettingAction::CheckForUpdates));
  system.push_back(SettingInfo::Action(StrId::STR_SD_FIRMWARE_UPDATE, SettingAction::SdFirmwareUpdate));
  system.push_back(SettingInfo::Action(StrId::STR_LANGUAGE, SettingAction::Language));

  const auto manageFonts = SettingInfo::Action(StrId::STR_MANAGE_FONTS, SettingAction::DownloadFonts);
  if (!reader.empty()) {
    reader.insert(reader.begin() + 1, manageFonts);
  } else {
    reader.push_back(manageFonts);
  }
  reader.push_back(SettingInfo::Action(StrId::STR_CUSTOMISE_STATUS_BAR, SettingAction::CustomiseStatusBar));
}

void buildSettings() {
  if (state.built) return;
  for (auto& list : state.settings) list.clear();

  sdFontSystem.refreshIfDirty();
  for (const auto& setting : getSettingsList(&sdFontSystem.registry())) {
    const int index = categoryIndex(setting.category);
    if (index >= 0) state.settings[static_cast<size_t>(index)].push_back(setting);
  }
  appendDeviceActions();
  state.built = true;
}

const SettingInfo* getSetting(uint32_t category, uint32_t index) {
  buildSettings();
  if (category >= kCategoryCount || index >= state.settings[category].size()) return nullptr;
  return &state.settings[category][index];
}

std::string settingValue(const SettingInfo& setting) {
  if (setting.type == SettingType::TOGGLE && setting.valuePtr) {
    return I18N.get((SETTINGS.*(setting.valuePtr)) ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
  }
  if (setting.type == SettingType::ENUM && setting.valuePtr) {
    const uint8_t value = SETTINGS.*(setting.valuePtr);
    if (value < setting.enumValues.size()) return I18N.get(setting.enumValues[value]);
  }
  if (setting.type == SettingType::ENUM && setting.valueGetter) {
    const uint8_t value = setting.valueGetter();
    if (!setting.enumStringValues.empty() && value < setting.enumStringValues.size()) {
      return setting.enumStringValues[value];
    }
    if (value < setting.enumValues.size()) return I18N.get(setting.enumValues[value]);
  }
  if (setting.type == SettingType::VALUE && setting.valuePtr) {
    return std::to_string(SETTINGS.*(setting.valuePtr));
  }
  if (setting.type == SettingType::TIMEZONE) {
    char name[TimeZoneCatalog::kMaxIdLength];
    TimeZoneCatalog::formatDisplayName(SETTINGS.timeZoneId, name, sizeof(name));
    return name;
  }
  if (setting.type == SettingType::STRING) {
    if (setting.stringGetter) return setting.stringGetter();
    if (setting.stringOffset && setting.stringMaxLen) {
      const char* value = reinterpret_cast<const char*>(&SETTINGS) + setting.stringOffset;
      return value;
    }
  }
  return {};
}

PendingAction mapAction(SettingAction action) {
  switch (action) {
    case SettingAction::RemapFrontButtons: return PendingAction::RemapFrontButtons;
    case SettingAction::CustomiseStatusBar: return PendingAction::CustomiseStatusBar;
    case SettingAction::KOReaderSync: return PendingAction::KOReaderSync;
    case SettingAction::OPDSBrowser: return PendingAction::OPDSBrowser;
    case SettingAction::Network: return PendingAction::Network;
    case SettingAction::BatteryStatus: return PendingAction::BatteryStatus;
    case SettingAction::ClearCache: return PendingAction::ClearCache;
    case SettingAction::CheckForUpdates: return PendingAction::CheckForUpdates;
    case SettingAction::SdFirmwareUpdate: return PendingAction::SdFirmwareUpdate;
    case SettingAction::Language: return PendingAction::Language;
    case SettingAction::DownloadFonts: return PendingAction::DownloadFonts;
    case SettingAction::None: default: return PendingAction::None;
  }
}

uint8_t requestAction(PendingAction action) {
  if (action == PendingAction::None) return T5_APP_SETTING_ERROR;
  state.requestedAction = action;
  return T5_APP_SETTING_ACTION_REQUESTED;
}

std::unique_ptr<Activity> makeActionActivity(PendingAction action, GfxRenderer& renderer, MappedInputManager& input) {
  switch (action) {
    case PendingAction::RemapFrontButtons:
      return std::make_unique<ButtonRemapActivity>(renderer, input);
    case PendingAction::CustomiseStatusBar:
      return std::make_unique<StatusBarSettingsActivity>(renderer, input);
    case PendingAction::KOReaderSync:
      return std::make_unique<KOReaderSettingsActivity>(renderer, input);
    case PendingAction::OPDSBrowser:
      return std::make_unique<OpdsServerListActivity>(renderer, input);
    case PendingAction::Network:
      return std::make_unique<WifiSelectionActivity>(renderer, input, false);
    case PendingAction::BatteryStatus:
      return std::make_unique<BatteryStatusActivity>(renderer, input);
    case PendingAction::ClearCache:
      return std::make_unique<ClearCacheActivity>(renderer, input);
    case PendingAction::CheckForUpdates:
      return std::make_unique<OtaUpdateActivity>(renderer, input);
    case PendingAction::SdFirmwareUpdate:
      return std::make_unique<SdFirmwareUpdateActivity>(renderer, input);
    case PendingAction::Language:
      return std::make_unique<LanguageSelectActivity>(renderer, input);
    case PendingAction::DownloadFonts:
      return std::make_unique<FontDownloadActivity>(renderer, input);
    case PendingAction::FontSelection:
      sdFontSystem.refreshIfDirty();
      return std::make_unique<FontSelectionActivity>(renderer, input, &sdFontSystem.registry());
    case PendingAction::TimeZone:
      return std::make_unique<TimeZoneSelectActivity>(renderer, input);
    case PendingAction::None:
    default:
      return nullptr;
  }
}

class NativeSettingsActionActivity final : public Activity {
  PendingAction action;
  std::string resumePath;
  bool started = false;
  bool childCompleted = false;
  bool resumeReturned = false;

 public:
  NativeSettingsActionActivity(GfxRenderer& renderer, MappedInputManager& input, PendingAction requested,
                               std::string resume)
      : Activity("NativeSettingsAction", renderer, input), action(requested), resumePath(std::move(resume)) {}

  void onEnter() override {
    Activity::onEnter();
    if (started) return;
    started = true;
    auto child = makeActionActivity(action, renderer, mappedInput);
    if (!child) {
      finish();
      return;
    }
    startActivityForResult(std::move(child), [this](const ActivityResult&) {
      SETTINGS.saveToFile();
      UITheme::getInstance().reload();
      childCompleted = true;
    });
  }

  void loop() override {
    if (resumeReturned) {
      finish();
      return;
    }
    if (!childCompleted) return;
    childCompleted = false;
    if (resumePath.empty()) {
      finish();
      return;
    }

    const esp_err_t result = runNativeApp(resumePath.c_str(), renderer, mappedInput);
    if (result != ESP_OK) {
      finish();
      return;
    }

    // Do not finish in this same iteration. If the resumed ELF requested
    // another firmware action, runNativeApp queued a new wrapper and the
    // ActivityManager will push it after loop() returns. Once that nested
    // wrapper unwinds, this instance finishes on its next loop iteration.
    resumeReturned = true;
  }

  void render(RenderLock&&) override {
    renderer.clearScreen();
    const auto& metrics = UITheme::getInstance().getMetrics();
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                   I18N.get(StrId::STR_SETTINGS_TITLE));
    renderer.displayBuffer(HalDisplay::BALANCED_REFRESH);
  }
};
}  // namespace

void nativeSettingsBegin(GfxRenderer& renderer, MappedInputManager& input) {
  state.renderer = &renderer;
  state.input = &input;
  state.requestedAction = PendingAction::None;
  state.built = false;
  for (auto& list : state.settings) list.clear();
}

void nativeSettingsEnd() {
  for (auto& list : state.settings) list.clear();
  state.built = false;
  state.renderer = nullptr;
  state.input = nullptr;
  UITheme::getInstance().reload();
}

uint32_t nativeSettingsCategoryCount() { return kCategoryCount; }

bool nativeSettingsCategoryGet(uint32_t category, char* label, size_t capacity) {
  if (category >= kCategoryCount || !label || capacity == 0) return false;
  copyText(label, capacity, I18N.get(kCategoryNames[category]));
  return true;
}

uint32_t nativeSettingsCount(uint32_t category) {
  buildSettings();
  if (category >= kCategoryCount) return 0;
  return static_cast<uint32_t>(state.settings[category].size());
}

bool nativeSettingsGet(uint32_t category, uint32_t index, t5_app_setting_t* out) {
  if (!out) return false;
  const SettingInfo* setting = getSetting(category, index);
  if (!setting) return false;
  *out = {};
  copyText(out->label, sizeof(out->label), I18N.get(setting->nameId));
  const std::string value = settingValue(*setting);
  copyText(out->value, sizeof(out->value), value.c_str());
  out->type = static_cast<uint8_t>(setting->type);
  return true;
}

uint8_t nativeSettingsActivate(uint32_t category, uint32_t index) {
  buildSettings();
  if (category >= kCategoryCount || index >= state.settings[category].size()) return T5_APP_SETTING_ERROR;
  SettingInfo& setting = state.settings[category][index];

  if (setting.type == SettingType::TOGGLE && setting.valuePtr) {
    SETTINGS.*(setting.valuePtr) = !(SETTINGS.*(setting.valuePtr));
  } else if (setting.type == SettingType::ENUM && setting.valuePtr) {
    if (setting.enumValues.empty()) return T5_APP_SETTING_ERROR;
    const uint8_t current = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = (current + 1) % static_cast<uint8_t>(setting.enumValues.size());
  } else if (setting.type == SettingType::ENUM && setting.valueGetter && setting.valueSetter) {
    if (setting.nameId == StrId::STR_FONT_FAMILY) return requestAction(PendingAction::FontSelection);
    const size_t count = setting.enumStringValues.empty() ? setting.enumValues.size() : setting.enumStringValues.size();
    if (count == 0 || count > 255) return T5_APP_SETTING_ERROR;
    setting.valueSetter((setting.valueGetter() + 1) % static_cast<uint8_t>(count));
  } else if (setting.type == SettingType::VALUE && setting.valuePtr) {
    const uint8_t current = SETTINGS.*(setting.valuePtr);
    const uint16_t next = static_cast<uint16_t>(current) + setting.valueRange.step;
    SETTINGS.*(setting.valuePtr) = next > setting.valueRange.max ? setting.valueRange.min : static_cast<uint8_t>(next);
  } else if (setting.type == SettingType::TIMEZONE) {
    return requestAction(PendingAction::TimeZone);
  } else if (setting.type == SettingType::ACTION) {
    return requestAction(mapAction(setting.action));
  } else {
    return T5_APP_SETTING_ERROR;
  }

  if (setting.valuePtr == &CrossPointSettings::backlightLevel) {
    Board::setBacklightLevel(SETTINGS.backlightLevel);
  }
  SETTINGS.saveToFile();
  return T5_APP_SETTING_UPDATED;
}

void nativeSettingsRender(uint32_t category, int32_t selectedIndex) {
  if (!state.renderer || !state.input) return;
  buildSettings();
  if (category >= kCategoryCount) category = 0;

  auto& renderer = *state.renderer;
  auto& input = *state.input;
  renderer.clearScreen();

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 I18N.get(StrId::STR_SETTINGS_TITLE), CROSSPOINT_VERSION);

  std::vector<TabInfo> tabs;
  tabs.reserve(kCategoryCount);
  for (uint32_t i = 0; i < kCategoryCount; ++i) {
    tabs.push_back({I18N.get(kCategoryNames[i]), category == i});
  }
  GUI.drawTabBar(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight}, tabs,
                 selectedIndex == 0);

  const auto& settings = state.settings[category];
  const int settingsCount = static_cast<int>(settings.size());
  GUI.drawList(
      renderer,
      Rect{0, metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing, pageWidth,
           pageHeight - (metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.buttonHintsHeight +
                         metrics.verticalSpacing * 2)},
      settingsCount, selectedIndex - 1,
      [&settings](int index) { return std::string(I18N.get(settings[index].nameId)); }, nullptr, nullptr,
      [&settings](int index) { return settingValue(settings[index]); }, true);

  const char* confirmLabel = selectedIndex == 0 ? I18N.get(kCategoryNames[(category + 1) % kCategoryCount])
                                                 : I18N.get(StrId::STR_TOGGLE);
  const auto labels = input.mapLabels(I18N.get(StrId::STR_BACK), confirmLabel, I18N.get(StrId::STR_DIR_UP),
                                      I18N.get(StrId::STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::BALANCED_REFRESH);
}

uint8_t nativeSettingsTouch(int16_t x, int16_t y, uint32_t* category, int32_t* selectedIndex) {
  if (!state.renderer || !category || !selectedIndex) return T5_APP_SETTING_ERROR;
  buildSettings();
  if (*category >= kCategoryCount) *category = 0;

  const int pageWidth = state.renderer->getScreenWidth();
  const int pageHeight = state.renderer->getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int tabTop = metrics.topPadding + metrics.headerHeight;
  const int tabBottom = tabTop + metrics.tabBarHeight;

  if (y >= tabTop && y < tabBottom) {
    *category = static_cast<uint32_t>(std::clamp((x * static_cast<int>(kCategoryCount)) / std::max(1, pageWidth), 0,
                                                 static_cast<int>(kCategoryCount) - 1));
    *selectedIndex = 0;
    return T5_APP_SETTING_UPDATED;
  }

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
  const int contentHeight =
      pageHeight -
      (metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.buttonHintsHeight +
       metrics.verticalSpacing * 2);
  const int rowHeight = metrics.listRowHeight;
  if (rowHeight <= 0 || y < contentTop || y >= contentTop + contentHeight) return T5_APP_SETTING_NO_CHANGE;

  const int count = static_cast<int>(state.settings[*category].size());
  const int pageItems = std::max(1, contentHeight / rowHeight);
  const int selectedRow = std::max(0, *selectedIndex - 1);
  const int pageStart = (selectedRow / pageItems) * pageItems;
  const int row = (y - contentTop) / rowHeight;
  const int touched = pageStart + row;
  if (row < 0 || row >= pageItems || touched < 0 || touched >= count) return T5_APP_SETTING_NO_CHANGE;

  *selectedIndex = touched + 1;
  return nativeSettingsActivate(*category, static_cast<uint32_t>(touched));
}

void nativeSettingsDispatchPendingAction(GfxRenderer& renderer, MappedInputManager& input, const char* resumePath) {
  const PendingAction action = state.requestedAction;
  state.requestedAction = PendingAction::None;
  if (action == PendingAction::None) return;
  activityManager.pushActivity(std::make_unique<NativeSettingsActionActivity>(
      renderer, input, action, resumePath ? std::string(resumePath) : std::string{}));
}
